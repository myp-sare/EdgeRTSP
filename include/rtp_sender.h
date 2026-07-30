/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<3598023002@qq.com>
 *                  All rights reserved.
 *
 *       Filename:  rtp_sender.h
 *    Description:  RTP sender public API: rtp_init / rtp_send_nalu / rtp_send_h264_file / rtp_close
 *                 
 *        Version:  1.0.0(2026/07/27)
 *         Author:  Mayanping <mayanping@email.com>
 *      ChangeLog:  1, Release initial version on "2026/07/27 11:32:02"
 *                 
 ********************************************************************************/

#ifndef RTP_SENDER_H
#define RTP_SENDER_H

#include <stdint.h>

typedef struct {
    uint8_t vpxcc;          // V=2, P=0, X=0, CC=0 → 0x80
    uint8_t mpt;            // M=0, PT=96（H264）→ 0x60
    uint16_t seq;           // 每包都变
    uint32_t timestamp;     // 每帧都变
    uint32_t ssrc;          // 会话内固定，启动时随机
} __attribute__((packed)) RTPHeader;        // RTP头部结构体

/* 初始化/销毁 */
int  rtp_init(const char *client_ip, int client_port);
void rtp_close(void);

/* 发送接口（核心） */
void rtp_send_nalu(const uint8_t *nalu_data, int nalu_len);

/* 文件推流（demo用） */
void rtp_send_h264_file(const char *filename);

#endif /* RTP_SENDER_H */