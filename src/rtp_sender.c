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
//#include <sys/stat.h>

static int rtp_sockfd = -1;
static struct sockaddr_in dest_addr;        //RTP 发送的目标地址（VLC 的 IP+端口）
static uint16_t seq = 0;
static uint32_t timestamp = 0;
static uint32_t ssrc = 0x12345678;

static uint8_t *h264_buffer = NULL;     //放当前整个H264文件
static int h264_buffer_len = 0;         //文件总大小
static int h264_pos = 0;                //当前读取位置

/*初始化 RTP socket*/
int rtp_init(const char *client_ip, int client_port)
{
    // 1.创建 UDP socket
    rtp_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (rtp_sockfd < 0)
    {
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

// 加载 H.264 文件到内存
static int load_h264_file(const char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp)
    {
        printf("文件打开失败：%s\n", filename);
        return -1;
    }
    
    fseek(fp, 0, SEEK_END);
    h264_buffer_len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    h264_buffer = malloc(h264_buffer_len);
    if(!h264_buffer)
    {
        printf("内存分配失败！\n");
        fclose(fp);
        return -1;
    }

    size_t read_len = fread(h264_buffer, 1, h264_buffer_len, fp);
    fclose(fp);

    if(read_len != h264_buffer_len)
    {
        printf("读取文件失败: 期望 %d, 实际 %zu\n", h264_buffer_len, read_len);
        free(h264_buffer);
        h264_buffer = NULL;
        return -1;
    }

    h264_pos = 0;
    printf("获取H.264裸流成功：%s, 大小=%d 字节\n", filename, h264_buffer_len);
    
    return 0;
}

// ========== 从h264裸流提取NALU ==========
static int get_next_nalu(uint8_t **nalu_start, int *nalu_len)
{
    if (h264_pos >= h264_buffer_len)
    {
        return -1;
    }
    
    // 1. 查找起始码：先检查 00 00 00 01 (4字节)
    int start_code_len = 0;
    if (h264_pos + 4 <= h264_buffer_len &&
        h264_buffer[h264_pos] == 0x00 &&
        h264_buffer[h264_pos + 1] == 0x00 &&
        h264_buffer[h264_pos + 2] == 0x00 &&
        h264_buffer[h264_pos + 3] == 0x01)
        {
        start_code_len = 4;
    }

    // 再检查 00 00 01 (3字节)
    else if (h264_pos + 3 <= h264_buffer_len &&
             h264_buffer[h264_pos] == 0x00 &&
             h264_buffer[h264_pos + 1] == 0x00 &&
             h264_buffer[h264_pos + 2] == 0x01)
             {
        start_code_len = 3;
    }
    else
    {
        // 如果找不到起始码，打印当前位置的16进制
        printf("在位置 %d 找不到起始码\n", h264_pos);
        printf("周围字节: ");
        for (int i = 0; i < 16 && h264_pos + i < h264_buffer_len; i++)
        {
            printf("%02X", h264_buffer[h264_pos + i]);
        }
        printf("\n");

        h264_pos++;

        return -1;
    }
    
    // 2. 跳过起始码
    int nalu_start_pos = h264_pos + start_code_len;
    
    // 3. 查找下一个起始码
    int next_start = -1;
    for (int i = nalu_start_pos + 1; i < h264_buffer_len; i++)
    {
        if (i + 4 <= h264_buffer_len &&
            h264_buffer[i] == 0x00 &&
            h264_buffer[i + 1] == 0x00 &&
            h264_buffer[i + 2] == 0x00 &&
            h264_buffer[i + 3] == 0x01)
            {
            next_start = i;
            break;
        }
        if (i + 3 <= h264_buffer_len &&
            h264_buffer[i] == 0x00 &&
            h264_buffer[i + 1] == 0x00 &&
            h264_buffer[i + 2] == 0x01)
            {
            next_start = i;
            break;
        }
    }
    
    // 4. 计算NALU长度
    if (next_start == -1)
    {
        *nalu_len = h264_buffer_len - nalu_start_pos;
    }
    else
    {
        *nalu_len = next_start - nalu_start_pos;
    }
    
    *nalu_start = h264_buffer + nalu_start_pos;
    h264_pos = (next_start == -1) ? h264_buffer_len : next_start;
    
    return 0;
}


/*发送一个 RTP 包（payload 已准备好，套上 RTP 头发出去）*/
static void rtp_send_packet(const uint8_t *payload, int payload_len, int mark_bit)
{
    if (rtp_sockfd < 0)
    {
        printf("RTP socket 未初始化，无法发送。\n");
        return;
    }

    uint8_t packet[1500];                       //一块裸字节缓冲区（1500字节）
    RTPHeader *header = (RTPHeader *)packet;    //把这块缓冲区内存"看成" RTPHeader
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
    if (sent < 0)
    {
        perror("RTP sendto 失败");
        return;
    }

    printf("RTP包已发送: seq=%d, timestamp=%u, size=%d\n\n",
           seq - 1, timestamp, total_size);
}

//大小NALU判断与大NALU分片，发送NALU
static void rtp_send_nalu(const uint8_t *nalu_data, int nalu_len)
{
    if (nalu_data == NULL || nalu_len <= 0)
    {
        return;
    }

    int max_payload = 1400 - 12;
    uint8_t nal_type = nalu_data[0] & 0x1F;

    // 小NALU：单包模式
    if (nalu_len <= max_payload)
    {
        rtp_send_packet(nalu_data, nalu_len, 1);
        return;
    }

    // 大NALU：FU-A分片
    int max_fu_payload = max_payload - 2;
    int payload_offset = 1;
    int remaining = nalu_len - 1;
    int is_first = 1;

    while (remaining > 0)
    {
        //如果还剩的数据比每片上限多，那这次就发满上限（1386）；否则，剩多少就发多少。
        int chunk = (remaining > max_fu_payload) ? max_fu_payload : remaining;
        int is_last = (chunk == remaining);

        uint8_t fu_packet[1500];
        RTPHeader *header = (RTPHeader *)fu_packet;
        header->vpxcc = 0x80;
        header->mpt = 0x60 | (is_last ? 0x80 : 0x00);
        header->seq = htons(seq++);
        header->timestamp = htonl(timestamp);
        header->ssrc = htonl(ssrc);

        //FU Indicator：保留原始 F+NRI，Type 强制设 28 (FU-A)
        fu_packet[12] = (nalu_data[0] & 0xE0) | 28;

        // FU Header：S/E 位 + 原始 Type
        fu_packet[13] = 0;
        if (is_first) fu_packet[13] |= 0x80;
        if (is_last)  fu_packet[13] |= 0x40;
        fu_packet[13] |= nal_type;

        memcpy(fu_packet + 14, nalu_data + payload_offset, chunk);

        int total_size = 12 + 2 + chunk;
        ssize_t sent = sendto(rtp_sockfd, fu_packet, total_size, 0,
                              (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (sent < 0)
        {
            perror("RTP FU-A sendto失败");
            return;
        }

        printf("FU-A分片: seq=%d, S=%d, E=%d, size=%d\n",
               seq - 1, is_first, is_last, total_size);

        payload_offset += chunk;
        remaining -= chunk;
        is_first = 0;
    }
}

void rtp_send_h264_file(const char *filename)
{
    //1.加载文件
    if (load_h264_file(filename) < 0)
    {
        printf("文件加载失败，停止推流。\n");
        return;
    }

    //2.重置序号和时间戳
    seq = 0;        //发送的包的序号，每发送成功一个，sep++
    timestamp = 0;  //每发送一帧+3000

    //3.循环发送
    uint8_t *nalu = NULL;
    int nalu_len = 0;
    int frame_count = 0;
    int nalu_count = 0;
    int sps_count = 0, pps_count = 0, idr_count = 0, sei_count = 0, slice_count = 0;

    while (get_next_nalu(&nalu, &nalu_len) == 0)
    {
        uint8_t nal_type = nalu[0] & 0x1F;
        nalu_count++;

        // 2.统计每种NALU的类型
        if (nal_type == 7) sps_count++;
        else if (nal_type == 8) pps_count++;
        else if (nal_type == 5) idr_count++;
        else if (nal_type == 6) sei_count++;
        else if (nal_type == 1) slice_count++;

        // 打印前 5 条 NALU
        if (nalu_count <= 5)
        {
            printf("%d. type=%d, len=%d\n", nalu_count, nal_type, nalu_len);
        }

        // 3.发送NALU
        rtp_send_nalu(nalu, nalu_len);

        // 时间戳推进：每处理完一帧（slice 或 IDR）推进一次
        // 同一 NALU 的所有 FU-A 分片共享同一个 timestamp
        if (nal_type == 1 || nal_type == 5)     //5是关键帧，1是P帧，这两种都是真正的图像数据
        {
            timestamp += 3000;   // 30fps: 90000/30 = 3000
            frame_count++;
            usleep(33000);       // 约 30fps 节奏，避免瞬间发完 VLC 缓冲不来
        }
    }

    printf("\n===== 推流完成 =====\n");
    printf("NALU总数: %d\n", nalu_count);
    printf("帧总数: %d\n", frame_count);
    printf("SPS=%d, PPS=%d, IDR=%d, SEI=%d, Slice=%d\n",
           sps_count, pps_count, idr_count, sei_count, slice_count);
    printf("====================\n\n");

    free(h264_buffer);
    h264_buffer = NULL;
}

void rtp_close(void)
{
    if (rtp_sockfd >= 0) {
        close(rtp_sockfd);
        rtp_sockfd = -1;
        printf("RTP socket 已关闭。\n");
    }
}