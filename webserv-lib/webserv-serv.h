#ifndef __WEBSERV_SERV_H
#define __WEBSERV_SERV_H

#include <errno.h>

#include "webserv-contype.h"

#ifndef EBADRQC
#define EBADRQC EINVAL
#endif

int server_start(const char *port, int backlog);
int server_accept(int servfd);
int server_handle_req(int conn_fd, const char *docroot, const char *servname,
                      httpmsg_t *req, httpmsg_t *res, const filetype_table_t *ftypes);
int server_handle_get(int conn_fd, const char *docroot, const char *servname, httpmsg_t *req,
                      httpmsg_t *res, const filetype_table_t *ftypes);

#endif
