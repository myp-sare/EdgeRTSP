/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<mayanping@email.com>
 *                  All rights reserved.
 *
 *       Filename:  rtsp_server.c
 *    Description:  
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
#include <sys/socket.h>     //socket,bind,listen,accept
#include <netinet/in.h>     //struct sockaddr_in
#include <arpa/inet.h>      //inet_ntoa,htons

#include "rtsp_server.h"
#include "rtp_sender.h"
#include "utils.h"

#define BUF_SIZE 4096

static int client_rtp_port = 0;
static int client_rtcp_port = 0;
static char client_ip[64] = {0};

static void send_options(int client_fd, int cseq)
{
    char response[512];

    snprintf(response, sizeof(response),
             "RTSP/1.0 200 OK\r\n"
             "CSeq: %d\r\n"
             "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN\r\n"
             "\r\n",
             cseq
    );

    send(client_fd, response, strlen(response), 0);
    printf("已发送 OPTIONS 响应: \n%s", response);
}

static void send_describe(int client_fd, int cseq)
{
    //1.构造SDP字符串
    const char *sdp =
        "v=0\r\n"                           //固定
        "o=- 0 0 IN IP4 0.0.0.0\r\n"    //理想状态下服务器真实地址
        "s=EdgeRTSP\r\n"                    //会话名，纯展示，随便写，不影响协议功能
        "t=0 0\r\n"                         //固定
        "a=control:*\r\n"               //会话级控制"*"表示"整个会话就是一个可控制的资源"
        "a=range:npt=0-\r\n"
        "m=video 0 RTP/AVP 96\r\n"      //媒体级（视频轨道）控制，真正端口在 SETUP 的 server_port 里协商
        "c=IN IP4 0.0.0.0\r\n"
        "a=rtpmap:96 H264/90000\r\n"        //H264/90000固定
        "a=fmtp:96 packetization-mode=1;"   //packetization-mode=1固定
        "sprop-parameter-sets="         //SPS/PPS 必须和实际推的 H264 码流一致（否则 VLC 解不出来）
        "Z2QQKKwbGqCgPaEAAAMAAQAAAwA8jwiEag==,"
        "aO88sA==\r\n"
        "a=control:track1\r\n";         //媒体级控制：这条轨道叫 track1

    char response[2048];
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

    if (response_len < 0 || response_len >= (int)sizeof(response)) {
        printf("DESCRIBE 响应太长，无法发送。\n");
        return;
    }
    
    send(client_fd, response, response_len, 0);
    printf("已发送 DESCRIBE 响应： \nCSeq=%d, SDP 长度=%zu\n\n", cseq, strlen(sdp));
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
    
    //1.创建socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) 
    {
        perror("socket 创建失败");
        return -1;
    }

    //2.设置SO_REUSEADDR选项,防止程序退出后，端口被系统占用几分钟（TIME_WAIT 状态）
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) 
    {
        perror("setsockopt 设置失败");
        close(server_fd);
        return -1;
    }

    //3.配置地址结构体
    server_addr.sin_family = AF_INET;           //IPV4
    server_addr.sin_port = htons(port);         //端口号转换为网络字节序
    server_addr.sin_addr.s_addr = INADDR_ANY;   //监听所有网卡

    //4.绑定socket到指定端口
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <0) 
    {
        perror("bind 绑定失败");
        close(server_fd);
        return -1;
    }

    //5.监听端口
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

    //无限循环等待客户端连接
    while (1)
    {
        //1.阻塞等待客户端连接
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

        //2.连接成功，打印客户端IP和端口号
        printf("VLC已连接: \nIP=%s, 端口=%d\n\n",
               inet_ntoa(client_address.sin_addr),
               ntohs(client_address.sin_port));

        //3..处理RTSP请求
        while(1)
        {
            memset(buf, 0, sizeof(buf));
            int read_len = recv(client_fd, buf, sizeof(buf) - 1, 0);
            if (read_len <= 0)
            {
                printf("VLC已断开连接, 等待下一个客户端...\n");
                break;
            }

            buf[read_len] = '\0';   //数据结束后的第一个位置写0，字符串结束标记
            printf("收到 RTSP 请求:\n%s", buf);

            //4.解析CSeq
            int cseq = get_vlc_cseq(buf);
            if (cseq < 0)
            {
                printf("未找到 CSeq, 跳过该请求。\n");
                continue;
            }

            //5.判断请求类型并处理
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
                send_play(client_fd, cseq);
                if (rtp_init(client_ip, client_rtp_port) < 0)
                {
                    printf("RTP 初始化失败,无法发送数据。\n\n");
                    continue;
                }
            
                rtp_send_hello();
            }
            else if(strstr(buf, "TEARDOWN"))
            {
                printf("VLC 请求断开连接...\n");

                rtp_close();
                break;  // 退出内层循环
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