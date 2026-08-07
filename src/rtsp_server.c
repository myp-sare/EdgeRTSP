/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<mayanping@email.com>
 *                  All rights reserved.
 *
 *       Filename:  rtsp_server.c
 *    Description:  Minimal RTSP server: handle OPTIONS/DESCRIBE/SETUP/PLAY/TEARDOWN, callback-based stream source
 *                 
 *        Version:  1.0.0(2026/07/27)
 *         Author:  Mayanping <mayanping@email.com>
 *      ChangeLog:  1, Release initial version on "2026/07/27 11:23:00"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>     // socket,bind,listen,accept
#include <netinet/in.h>     // struct sockaddr_in
#include <arpa/inet.h>      // inet_ntoa,htons
#include <pthread.h>

#include "rtsp_server.h"
#include "rtp_sender.h"
#include "utils.h"

#define BUF_SIZE 4096

// ===== 动态SPS/PPS存储 =====
static char g_sps_base64[128] = {0};
static char g_pps_base64[128] = {0};
static int g_sps_pps_ready = 0;
static pthread_mutex_t g_sps_mutex = PTHREAD_MUTEX_INITIALIZER;

// ===== Base64编码 =====
static void base64_encode(const uint8_t *input, int len, char *output)
{
    const char *table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i = 0, j = 0;
    uint8_t a, b, c;
    
    while (i < len) {
        a = input[i++];
        b = (i < len) ? input[i++] : 0;
        c = (i < len) ? input[i++] : 0;
        
        output[j++] = table[a >> 2];
        output[j++] = table[((a & 0x03) << 4) | (b >> 4)];
        output[j++] = (i - 2 < len) ? table[((b & 0x0f) << 2) | (c >> 6)] : '=';
        output[j++] = (i - 1 < len) ? table[c & 0x3f] : '=';
    }
    output[j] = '\0';
}

// ===== 外部接口：设置SPS/PPS =====
void rtsp_set_sps_pps(const uint8_t *sps, int sps_len, 
                       const uint8_t *pps, int pps_len)
{
    pthread_mutex_lock(&g_sps_mutex);
    
    if (sps != NULL && sps_len > 0) {
        memset(g_sps_base64, 0, sizeof(g_sps_base64));
        base64_encode(sps, sps_len, g_sps_base64);
    }
    if (pps != NULL && pps_len > 0) {
        memset(g_pps_base64, 0, sizeof(g_pps_base64));
        base64_encode(pps, pps_len, g_pps_base64);
    }
    
    if (strlen(g_sps_base64) > 0 && strlen(g_pps_base64) > 0) {
        g_sps_pps_ready = 1;
        printf("SPS/PPS 已完整就绪!\n");
    }
    
    pthread_mutex_unlock(&g_sps_mutex);
}

// ===== 查询 SPS/PPS 是否已就绪 =====
int rtsp_is_sps_pps_ready(void)
{
    return g_sps_pps_ready;
}

static volatile int g_streaming = 0;           // 流状态信号灯：0停 1播
static pthread_t g_stream_tid;                 // 记录推流线程ID，用于 join
static void *(*play_action)(void *) = NULL;    // 回调空位
static int client_rtp_port = 0;
static int client_rtcp_port = 0;
static char client_ip[64] = {0};

// 给外部注册回调
void rtsp_set_play_action(void *(*action)(void *))
{
    play_action = action;
}

// 给 camera_loop 读状态
int rtsp_is_streaming(void)
{
    return g_streaming;
}

static void send_options(int client_fd, int cseq)
{
    char response[512];

    snprintf(response, sizeof(response),
             "RTSP/1.0 200 OK\r\n"
             "CSeq: %d\r\n"
             "Public: OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN\r\n"
             "\r\n",
             cseq
    );

    send(client_fd, response, strlen(response), 0);
    printf("已发送 OPTIONS 响应: \n%s", response);
}

static void send_describe(int client_fd, int cseq)
{
    char sps_pps[512];
    char sdp[2048];
    int wait_count = 0;
    
    // ===== 等待 SPS/PPS 就绪（最多等待2秒）=====
    while (!g_sps_pps_ready && wait_count < 40)
    {
        usleep(50000);
        wait_count++;
    }
    
    pthread_mutex_lock(&g_sps_mutex);
    
    if (g_sps_pps_ready && strlen(g_sps_base64) > 0 && strlen(g_pps_base64) > 0)
    {
        snprintf(sps_pps, sizeof(sps_pps),
                 "sprop-parameter-sets=%s,%s",
                 g_sps_base64, g_pps_base64);
        printf("使用动态SPS/PPS (等待%dms)\n", wait_count * 50);
    } else
    {
        snprintf(sps_pps, sizeof(sps_pps),
                 "sprop-parameter-sets=Z2QAKKzN2QFAFuaAQCAAAAMAAQAAAwA8h4UYAQ==,aO48sA==");
        printf("SPS/PPS超时, 使用占位符\n");
    }
    
    pthread_mutex_unlock(&g_sps_mutex);
    
    snprintf(sdp, sizeof(sdp),
        "v=0\r\n"
        "o=- 0 0 IN IP4 0.0.0.0\r\n"
        "s=EdgeRTSP\r\n"
        "t=0 0\r\n"
        "a=control:*\r\n"
        "a=range:npt=0-\r\n"
        "m=video 0 RTP/AVP 96\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=rtpmap:96 H264/90000\r\n"
        "a=fmtp:96 packetization-mode=1;%s\r\n"
        "a=control:track1\r\n",
        sps_pps
    );

    char response[4096];
    int response_len = snprintf(response, sizeof(response),
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %d\r\n"
        "Content-Type: application/sdp\r\n"
        "Content-Length: %zu\r\n"
        "\r\n"
        "%s",
        cseq,
        strlen(sdp),
        sdp
    );

    send(client_fd, response, response_len, 0);
    printf("已发送 DESCRIBE 响应 (SDP长度=%zu)\n", strlen(sdp));
}

static void send_setup(int client_fd, int cseq)
{
    char response[512];

    snprintf(response, sizeof(response),
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %d\r\n"
        "Session: 12345678\r\n"
        "Transport: RTP/AVP;unicast;client_port=%d-%d;server_port=6000-6001\r\n"
        "\r\n",
        cseq,
        client_rtp_port,
        client_rtcp_port
    );

    send(client_fd, response, strlen(response), 0);
    printf("已发送 SETUP 响应： \nCSeq=%d, client_port=%d-%d, Session=12345678\n\n", 
           cseq, client_rtp_port, client_rtcp_port);
}

static void send_play(int client_fd, int cseq)
{
    char response[512];
    snprintf(response, sizeof(response),
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %d\r\n"
        "Session: 12345678\r\n"
        "RTP-Info: url=rtsp://0.0.0.0:8554/live/track1;seq=0;rtptime=0\r\n"
        "\r\n",
        cseq
    );

    send(client_fd, response, strlen(response), 0);
    printf("已发送 PLAY 响应: \nCSeq=%d, nRTP-Info: seq=0, rtptime=0\n\n", cseq);
}

/*初始化RTSP服务器(创建socket，绑定端口，开始监听)*/
int rtsp_server_init(int port)
{
    int server_fd;
    struct sockaddr_in server_addr;
    int opt = 1;
    
    // 1. 创建socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) 
    {
        perror("socket 创建失败");
        return -1;
    }

    // 2. 设置SO_REUSEADDR选项,防止程序退出后，端口被系统占用几分钟（TIME_WAIT 状态）
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) 
    {
        perror("setsockopt 设置失败");
        close(server_fd);
        return -1;
    }

    // 3. 配置地址结构体
    server_addr.sin_family = AF_INET;           // IPV4
    server_addr.sin_port = htons(port);         // 端口号转换为网络字节序
    server_addr.sin_addr.s_addr = INADDR_ANY;   // 监听所有网卡

    // 4. 绑定socket到指定端口
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <0) 
    {
        perror("bind 绑定失败");
        close(server_fd);
        return -1;
    }

    // 5. 监听端口
    if (listen(server_fd, 3) < 0)
    {
        perror("listen 监听失败");
        close(server_fd);
        return -1;
    }

    printf("RTSP 服务器启动成功正在监听端口 %d\n", port);

    return server_fd;
}

/*运行RTSP服务器，处理客户端请求*/
void rtsp_server_run(int server_fd)
{
    struct sockaddr_in client_address;
    socklen_t client_len = sizeof(client_address);
    char buf[BUF_SIZE];

    printf("RTSP 服务器已就绪, 等待 VLC 连接...\n");

    // 无限循环等待客户端连接
    while (1)
    {
        // 1. 阻塞等待客户端连接
        //  accept 返回一个新的 socket，用于和客户端通信
        int client_fd;
        client_fd = accept(server_fd, (struct sockaddr *)&client_address, &client_len);
        if (client_fd < 0)
        {
            perror("accept 失败");
            continue;
        }

        // 提取 VLC 的 IP 字符串（二进制 sin_addr → 文本，供 rtp_init 使用）
        strncpy(client_ip, inet_ntoa(client_address.sin_addr), sizeof(client_ip) - 1);
        // 防御：strncpy 在源串超长时不补 \0，手动确保结尾
        client_ip[sizeof(client_ip) - 1] = '\0';

        // 2. 连接成功，打印客户端IP和端口号
        printf("VLC已连接: \nIP=%s, 端口=%d\n\n",
               inet_ntoa(client_address.sin_addr),
               ntohs(client_address.sin_port));

        // 3. 处理RTSP请求
        while(1)
        {
            memset(buf, 0, sizeof(buf));
            int read_len = recv(client_fd, buf, sizeof(buf) - 1, 0);
            if (read_len <= 0)
            {
                printf("VLC已断开连接, 等待下一个客户端...\n");
                break;
            }

            buf[read_len] = '\0';   // 数据结束后的第一个位置写0，字符串结束标记
            printf("收到 RTSP 请求:\n%s", buf);

            // 4. 解析CSeq
            int cseq = get_vlc_cseq(buf);
            if (cseq < 0)
            {
                printf("未找到 CSeq, 跳过该请求。\n");
                continue;
            }

            // 5. 判断请求类型并处理
            if(strstr(buf,"OPTIONS"))
            {
                send_options(client_fd, cseq);
            }
            else if(strstr(buf,"DESCRIBE"))
            {
                send_describe(client_fd,cseq);
            }
            else if(strstr(buf,"SETUP"))
            {
                if (parse_client_ports(buf, &client_rtp_port, &client_rtcp_port) < 0)
                {
                    char bad_request[128];
                    snprintf(bad_request, sizeof(bad_request),
                            "RTSP/1.0 400 bad_request\r\nCSeq: %d\r\n\r\n", cseq);
                    send(client_fd, bad_request, strlen(bad_request), 0);
                    continue;
                }
                
                send_setup(client_fd, cseq);
            }
            else if(strstr(buf, "PLAY"))
            {
                g_streaming = 1;   // 先亮绿灯
                send_play(client_fd, cseq);
                if (rtp_init(client_ip, client_rtp_port) < 0)
                {
                    printf("RTP 初始化失败,无法发送数据。\n\n");
                    g_streaming = 0;                 // 失败要灭灯，别留脏状态
                    continue;
                }

                if (play_action != NULL)
                {
                    // 摄像头模式：起线程跑 camera_loop（play_action 指向它）
                    pthread_create(&g_stream_tid, NULL, play_action, NULL);
                } else
                {
                    // 文件模式兜底：直接发文件（你原来的逻辑，暂不动）
                    rtp_send_h264_file("/home/mayanping/workspace/EdgeRTSP/output.h264");
                }
            }
            else if(strstr(buf, "PAUSE"))
            {
                printf("VLC 请求暂停播放...\n");
                g_streaming = 0;   // 灭绿灯，亮红灯通知线程暂停
                // 这里不 join 线程，camera_loop 里会自己停在 while 循环里
                char response[128];
                snprintf(response, sizeof(response),
                        "RTSP/1.0 200 OK\r\nCSeq: %d\r\n\r\n", cseq);
                send(client_fd, response, strlen(response), 0);
            }
            else if(strstr(buf, "TEARDOWN"))
            {
                printf("VLC 请求断开连接...\n");

                g_streaming = 0;                      // ① 亮红灯，通知线程退出
                if (play_action != NULL)              // ← 只有摄像头模式才 join
                {
                    pthread_join(g_stream_tid, NULL); // ② 等线程真正退出（跑完当前帧）
                }
                rtp_close();                          // ③ 确认无人用 socket 再关
                break;                                // ④ 退内层循环
            }
            else
            {
                printf("未知请求类型,暂不处理。\n");
                
                char not_found[128];
                snprintf(not_found, sizeof(not_found),
                        "RTSP/1.0 404 Not Found\r\nCSeq: %d\r\n\r\n\n", cseq);
                send(client_fd, not_found, strlen(not_found), 0);
            }
        }

        close(client_fd);
        printf("VLC已断开连接, 等待下一个客户端...\n\n");
    }
}