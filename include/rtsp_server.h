/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<3598023002@qq.com>
 *                  All rights reserved.
 *
 *       Filename:  rtsp_server.h
 *    Description:  RTSP server public API + callback registration for PLAY action
 *                 
 *        Version:  1.0.0(2026/07/27)
 *         Author:  Mayanping <mayanping@email.com>
 *      ChangeLog:  1, Release initial version on "2026/07/27 11:25:15"
 *                 
 ********************************************************************************/

#ifndef RTSP_SERVER_H
#define RTSP_SERVER_H

#include <stdint.h>

/* 初始化/运行 */
int rtsp_server_init(int port);
void rtsp_server_run(int server_fd);

/* 回调注册 */
void rtsp_set_play_action(void *(*action)(void *));    // 册 PLAY 时要执行的函数（回调）

/* 状态查询 */
int rtsp_is_streaming(void);                        // 让外部（camera_loop）能读流状态

/* 动态SPS/PPS设置（由编码器调用） */
void rtsp_set_sps_pps(const uint8_t *sps, int sps_len, 
                       const uint8_t *pps, int pps_len);
                       
#endif /* RTSP_SERVER_H */
