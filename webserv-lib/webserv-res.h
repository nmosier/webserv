#ifndef __WEBSERV_RES_H
#define __WEBSERV_RES_H

/* required headers */
#include "webserv-msg.h"
#include "webserv-contype.h"

/* macros/defines */

/* HTTP response codes */
#define C_OK        200
#define C_NOTFOUND  404
#define C_FORBIDDEN 403

#define C_NOTFOUND_BODY  "Not Found"
#define C_FORBIDDEN_BODY "Forbidden"

/* prototypes */
void response_init(httpmsg_t *res);
void response_delete(httpmsg_t *res);
int response_insert_line(int code, const char *version, httpmsg_t *res);
int response_insert_header(const char *key, const char *val, httpmsg_t *res);
int response_insert_body(const void *body, size_t bodylen, const char *type, httpmsg_t *res);
int response_insert_file(const char *path, httpmsg_t *res, const filetype_table_t *ftypes);
int response_insert_genhdrs(httpmsg_t *res);
int response_insert_servhdrs(const char *servname, httpmsg_t *res);
httpres_stat_t *response_find_status(int code);
int response_send(int conn_fd, httpmsg_t *res);

#endif
