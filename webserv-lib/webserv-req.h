#ifndef __WEBSERV_REQ_H
#define __WEBSERV_REQ_H

/* required headers */
#include "webserv-msg.h"

/* constants */
enum {
   DOC_FIND_ESUCCESS = 0,
   DOC_FIND_EINTERNAL,
   DOC_FIND_ENOTFOUND
};


/* prototypes */
void request_init(httpmsg_t *req);
int request_read(int conn_fd, httpmsg_t *req);
int request_parse(httpmsg_t *req);
void request_delete(httpmsg_t *req);
int request_document_find(const char *docroot, char **pathp, httpmsg_t *req);

#endif
