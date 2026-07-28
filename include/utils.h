/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<3598023002@qq.com>
 *                  All rights reserved.
 *
 *       Filename:  utils.h
 *    Description:  
 *                 
 *        Version:  1.0.0(2026/07/27)
 *         Author:  Mayanping <mayanping@email.com>
 *      ChangeLog:  1, Release initial version on "2026/07/27 11:19:39"
 *                 
 ********************************************************************************/

#ifndef UTILS_H
#define UTILS_H

int get_vlc_cseq(const char *req);

int parse_client_ports(const char *req, int *rtp_port, int *rtcp_port);

#endif /* UTILS_H */
