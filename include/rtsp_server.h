/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<3598023002@qq.com>
 *                  All rights reserved.
 *
 *       Filename:  rtsp_server.h
 *    Description:  
 *                 
 *        Version:  1.0.0(2026/07/27)
 *         Author:  Mayanping <mayanping@email.com>
 *      ChangeLog:  1, Release initial version on "2026/07/27 11:25:15"
 *                 
 ********************************************************************************/

#ifndef RTSP_SERVER_H
#define RTSP_SERVER_H

#include <stdint.h>

int rtsp_server_init(int port);
void rtsp_server_run(int server_fd);

#endif /* RTSP_SERVER_H */
