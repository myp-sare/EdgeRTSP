/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<mayanping@email.com>
 *                  All rights reserved.
 *
 *       Filename:  rtp_sender.c
 *    Description:  
 *                 
 *        Version:  1.0.0(2026/07/27)
 *         Author:  Mayanping <mayanping@email.com>
 *      ChangeLog:  1, Release initial version on "2026/07/27 11:22:49"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "rtp_sender.h"

static int rtp_sockfd = -1;
static struct sockaddr_in dest_addr;        //RTP 发送的目标地址（VLC 的 IP+端口）
static uint16_t seq = 0;
static uint32_t timestamp = 0;
static uint32_t ssrc = 0x12345678;

/*初始化 RTP socket*/
int rtp_init(const char *client_ip, int client_port)
{
    // 1.创建 UDP socket
    rtp_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (rtp_sockfd < 0) {
        perror("rtp socket 创建失败");
        return -1;
    }

    //2.保存目标地址（VLC 的 IP 和 RTP 端口）
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(client_port);
    dest_addr.sin_addr.s_addr = inet_addr(client_ip);
    
    printf("RTP 初始化成功，目标:\nIP= %s, 端口= %d\n", client_ip, client_port);
    return 0;
}

/*发送一个 RTP 测试包（Hello 字符串）*/
void rtp_send_hello(void)
{
    if (rtp_sockfd < 0) {
        printf("RTP socket 未初始化，无法发送。\n");
        return;
    }

    char packet[1500];                          //一块裸字节缓冲区（1500字节）
    RTPHeader *header = (RTPHeader *)packet;    //把这块缓冲区内存"看成" RTPHeader
    const char *payload = "Hello VLC! This is RTP from EdgeRTSP!";
    int payload_len = strlen(payload);

    //1.填充 RTP 头（12 字节）进入到packet数组
    header->vpxcc = 0x80;                       // V=2, P=0, X=0, CC=0
    header->mpt = 0x60;                         // M=0, PT=96 (H264)
    header->seq = htons(seq++);
    header->timestamp = htonl(timestamp);
    header->ssrc = htonl(ssrc);

    //2.复制发送数据
    memcpy(packet + 12, payload, payload_len);

    //3.发送 UDP 包
    int total_size = 12 + payload_len;
    ssize_t sent = sendto(rtp_sockfd, packet, total_size, 0,
                          (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (sent < 0) {
        perror("RTP sendto 失败");
        return;
    }

    printf("RTP 包已发送: seq=%d, timestamp=%u, size=%d\n\n",
           seq - 1, timestamp, total_size);

    // 4. 更新时间戳（30fps: 90000/30 = 3000）
    timestamp += 3000;
}

void rtp_close(void)
{
    if (rtp_sockfd >= 0) {
        close(rtp_sockfd);
        rtp_sockfd = -1;
        printf("RTP socket 已关闭。\n");
    }
}