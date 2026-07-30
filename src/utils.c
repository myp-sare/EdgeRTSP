/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<mayanping@email.com>
 *                  All rights reserved.
 *
 *       Filename:  utils.c
 *    Description:  RTSP request helpers: parse CSeq, parse client ports from SETUP
 *                 
 *        Version:  1.0.0(2026/07/27)
 *         Author:  Mayanping <mayanping@email.com>
 *      ChangeLog:  1, Release initial version on "2026/07/27 11:18:40"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include <string.h>
#include "utils.h"

// 从RTSP请求中获取CSeq值
int get_vlc_cseq(const char *req)
{
    const char *cseq_str = strstr(req, "CSeq: ");
    if (!cseq_str)
    {
        return -1;      // 未找到CSeq
    }
    int cseq;
    if (sscanf(cseq_str, "CSeq: %d", &cseq) == 1)
    {
        return cseq;    // 找到且成功解析
    }

    return -1;          // 解析失败
}

// 从 SETUP 请求中提取 VLC 的 RTP 和 RTCP 端口
int parse_client_ports(const char *req, int *rtp_port, int *rtcp_port)
{
    // 从 SETUP 请求中解析 "client_port=xxxx-yyyy"
    const char *vlc_port = strstr(req, "client_port=");
    if(!vlc_port)       // 未找到端口
    {
        return -1;
    }
    if(sscanf(vlc_port, "client_port=%d-%d",rtp_port, rtcp_port ) == 2)
    {
        return 0;       // 找到且解析成功
    }

    return -1;          // 解析失败
}
