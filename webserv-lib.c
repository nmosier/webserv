#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "webserv-lib.h"
#include "webserv-util.h"

// prints errors
int server_start(const char *port, int backlog) {
   int servsock_fd;
   struct addrinfo *res;
   int gai_stat;
   int error;

   /* initialize variables */
   servsock_fd = -1;
   res = NULL;
   error = 0; // error=1 if error occurred
   
   /* obtain (nonblocking) socket */
   if ((servsock_fd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0)) < 0) {
      perror("socket");
      error = 1;
      goto cleanup;
   }

   /* get address info */
   struct addrinfo hints = {0};
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_flags = AI_PASSIVE;
   if ((gai_stat = getaddrinfo(NULL, port, &hints, &res))) {
      /* getaddrinfo() error */
      fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gai_stat));
      error = 1;
      goto cleanup;
   }
   
   /* bind socket to port */
   if (bind(servsock_fd, res->ai_addr, res->ai_addrlen) < 0) {
      perror("bind");
      error = 1;
      goto cleanup;
   }

   /* listen for connections */
   if (listen(servsock_fd, backlog) < 0) {
      perror("listen");
      error = 1;
      goto cleanup;
   }

 cleanup:
   /* close server socket (if error occurred) */
   if (error && servsock_fd >= 0 && close(servsock_fd) < 0) {
      perror("close");
   }

   /* free res addrinfo (unconditionally) */
   if (res) {
      freeaddrinfo(res);
   }

   /* return -1 on error, server socket on success */
   return error ? -1 : servsock_fd;
}

// EXPLAIN DESIGN DECISION
// nonblocking -- so super-slow connections don't gum up the works
// returns -1 & error EAGAIN or EWOULDBLOCK if not available
// if *req is null, then create new. Else resume read.

int message_init(httpmsg_t *msg) {
   /* initialize struct fields */
   memset(msg, 0, sizeof(httpmsg_t));
   msg->hm_body = -1;

   /* allocate & initialize buffer */
   msg->hm_text_endp = msg->hm_text = malloc(HTTPMSG_TEXTSZ + 1); // +1 for null terminator
   if (msg->hm_text == NULL) {
      return -1;
   }
   msg->hm_text_size = HTTPMSG_TEXTSZ;
   
   /* allocate & initialize headers */
   msg->hm_headers = msg->hm_headers_endp = calloc(HTTPMSG_NHEADS,
                                                   sizeof(httpmsg_header_t));
   if (msg->hm_headers == NULL) {
      return -1;
   }
   msg->hm_nheaders = HTTPMSG_NHEADS;

   return 0;
}


int request_read(int servsock_fd, int conn_fd, httpmsg_t *req) {
   int msg_done;
   ssize_t bytes_received;
   size_t bytes_free;

   /* read until block, EOF, or \r\n */
   msg_done = 0; // whether terminating \r\n has been encountered
   do {
      /* resize text buffer if necessary */
      bytes_free = HM_TEXTFREE(req);
      if (bytes_free == 0) {
         if (message_resize_text(req->hm_text_size * 2, req) < 0) {
            perror("request_resize_text");
            return REQ_RD_RERROR;
         }
         bytes_free = HM_TEXTFREE(req); // update free byte count
      }

      /* receive bytes */
      bytes_received = recv(conn_fd, req->hm_text_endp, bytes_free, MSG_DONTWAIT);

      /* if bytes received, update request fields */
      if (bytes_received > 0) {
         /* update text buffer fields */
         req->hm_text_endp += bytes_received;
         *(req->hm_text_endp) = '\0';
         
         /* check for terminating line */
         if (req->hm_text_endp - req->hm_text >= 4 &&
             strcmp("\r\n\r\n", req->hm_text_endp - 4) == 0) {
            msg_done = 1;
         }
      }
   } while (bytes_received > 0 && !msg_done);

   /* check for errors */
   if (bytes_received < 0) {
      /* check if due to blocking */
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
         return REQ_RD_RAGAIN;
      }
      /* otherwise, report error */
      perror("recv");
      return REQ_RD_RERROR;
   }
   
   return REQ_RD_RSUCCESS;
}





int request_parse(httpmsg_t *req) {
   char *saveptr_text;

   ////////// PARSE REQUEST LINE /////////
   char *req_line, *req_method_str, *req_version;
   req_line = strtok_r(req->hm_text, "\n", &saveptr_text); // has trailing '\r'

   /* parse request line method */
   if ((req_method_str = strtok(req_line, " "))) {
      req->hm_line.reql.method = hr_str2meth(req_method_str);
   }
   if (req_method_str == NULL || req->hm_line.reql.method < 0) {
      return REQ_PRS_RSYNTAX;
   }

   /* parse request line URI */
   if ((req->hm_line.reql.uri = strtok(NULL, " ")) == NULL) {
      return REQ_PRS_RSYNTAX;
   }

   /* parse request line HTTP version */
   req_version = strtok(NULL, "\r"); // last item in line
   if (req_version && (req->hm_line.reql.version = strskip(HM_VERSION_PREFIX, req_version))) {
   } else {
      return REQ_PRS_RSYNTAX;
   }
   
   ///////// PARSE REQUEST HEADERS /////////
   httpmsg_header_t *header_it;
   char *header_str, *val_str, *key_str;
   for (header_it = req->hm_headers;
        (header_str = strtok_r(NULL, "\n", &saveptr_text))
           && strcmp(header_str, "\r");
        ++header_it) {

      /* check if array full */
      if (header_it == req->hm_headers + req->hm_nheaders) {
         size_t header_i = header_it - req->hm_headers; // current index
         size_t new_nheaders = req->hm_nheaders * 2;

         /* expand header size */
         if (message_resize_headers(new_nheaders, req) < 0) {
            return REQ_PRS_RERROR;
         }

         header_it = req->hm_headers + header_i; // update header iterator         
      }
      
      /* parse single request header */

      /* get key */
      if ((key_str = strtok(header_str, ":")) == NULL) {
         return REQ_PRS_RSYNTAX;
      }
      /* get value */
      if ((val_str = strtok(NULL, "\r")) == NULL) {
         return REQ_PRS_RSYNTAX;
      } else {
         /* strip leading whitespace */
         val_str = strstrip(val_str, " ");
      }
      /* set key & value */
      header_it->key = HM_STR2OFF(key_str, req);
      header_it->value = HM_STR2OFF(val_str, req);
   }
   
   return REQ_PRS_RSUCCESS;
}

void message_delete(httpmsg_t *msg) {
   if (msg) {
      /* free members */
      free(msg->hm_headers);
      free(msg->hm_text);
      
      /* free request */
      free(msg);
   }
}

int message_resize_headers(size_t new_nheaders, httpmsg_t *msg) {
   httpmsg_header_t *newheaders;

   /* reallocate headers array */
   newheaders = realloc(msg->hm_headers, (new_nheaders+1) * sizeof(httpmsg_header_t));
   if (newheaders == NULL) {
      return -1;
   }
   msg->hm_headers = newheaders;
   
   /* zero out uninitialized memory */
   if (new_nheaders > msg->hm_nheaders) {
      memset(newheaders + msg->hm_nheaders + 1, 0,
             sizeof(httpmsg_header_t) * (new_nheaders - msg->hm_nheaders));
   } else {
      /* zero out last element */
      memset(newheaders + new_nheaders, 0, sizeof(httpmsg_header_t));
   }
   msg->hm_nheaders = new_nheaders;

   return 0;
}

int message_resize_text(size_t newsize, httpmsg_t *msg) {
   char *newtext;

   /* reallocate text buffer */
   newtext = realloc(msg->hm_text, newsize + 1);
   if (newtext == NULL) {
      return -1;
   }
   msg->hm_text_size = newsize;
   msg->hm_text_endp += newtext - msg->hm_text;
   msg->hm_text = newtext;

   return 0;
}


static httpres_stat_t hr_stats[] = {
   {200, "OK"},
   {404, "Not found"}
};




/// response stuff
int response_header_insert(const char *key, const char *val, httpmsg_t *res) {
   httpmsg_header_t *hdr_it;
   size_t key_len, val_len;
   char *text_ptr;

   /* resize if full */
   if (res->hm_headers_endp == res->hm_headers + res->hm_nheaders) {
      if (message_resize_headers(res->hm_nheaders * 2, res) < 0) {
         perror("message_resize_headers");
         return -1;
      }
   }
   
   /* find next available element */
   for (hdr_it = res->hm_headers; hdr_it < res->hm_headers_endp; ++hdr_it) {}
   
   /* find length of header's underlying strings */
   key_len = strlen(key) + 1;
   val_len = strlen(val) + 1;

   /* make sure there's enough space to copy strings into response */
   if (HM_TEXTFREE(res) < key_len + val_len) {
      if (message_resize_text(res->hm_text_size * 2, res) < 0) {
         perror("message_resize_text");
         return -1;
      }
   }
   text_ptr = res->hm_text_endp + 1; // +1 so previous strings are terminated by '\0'

   /* copy header key string & pointer */
   memcpy(text_ptr, key, key_len); // +1 for null terminator
   hdr_it->key = HM_STR2OFF(text_ptr, res);

   text_ptr += key_len;
   
   /* copy header into array */
   memcpy(text_ptr, val, val_len);
   hdr_it->value = HM_STR2OFF(text_ptr, res);

   res->hm_text_endp += key_len + val_len;
   ++res->hm_headers_endp;
   
   return 0;
}


int response_body_insert(const char *body, httpmsg_t *res) {
   size_t body_len = strlen(body) + 1;

   /* resize response's text if necessary */
   if (HM_TEXTFREE(res) < body_len) {
      if (message_resize_text(res->hm_text_size * 2, res) < 0) {
         return -1;
      }
   }
   
   /* copy body into response's text */
   memcpy(res->hm_text_endp + 1, body, body_len); // +1 for null term.

   /* update response pointers */
   res->hm_body = HM_STR2OFF(res->hm_text_endp + 1, res);
   res->hm_text_endp += body_len;

   return 0;
}

// generate & send response
// use dprintf!
// know that text size is enough to fit all headers

int response_send(int conn_fd, httpmsg_t *res) {
   const char *res_fmt;
   char *hdrs_str, *hdrs_str_it, *hdr_key, *hdr_val, *body;
   size_t hdrs_str_len;
   httpmsg_header_t *hdr_it;
   httpres_line_t *line;
   httpres_stat_t *status;
   int send_status;

   if (res->hm_body < 0) {
      res->hm_body = HM_STR2OFF(res->hm_text_endp, res);
      // ensure message has a body, even if empty string
   }

   /* format response line */
   
   /* format headers string */
   hdrs_str_len = res->hm_text_size + (HM_HDR_SEPLEN+HM_ENT_TERMLEN)*res->hm_nheaders;
   hdrs_str = malloc(hdrs_str_len + 1); // +1 for null terminator
   for (hdr_it = res->hm_headers, hdrs_str_it = hdrs_str;
        hdr_it != res->hm_headers_endp; ++hdr_it) {
      hdr_key = HM_OFF2STR(hdr_it->key, res);
      hdr_val = HM_OFF2STR(hdr_it->value, res);
      sprintf(hdrs_str_it, "%s"HM_HDR_SEP"%s"HM_ENT_TERM, hdr_key, hdr_val);
   }

   /* format & send entire message */
   res_fmt =
      HM_VERSION_PREFIX"%s %d %s"HM_ENT_TERM   // response line
      "%s"HM_ENT_TERM                          // response headers
      "%s";                                    // response body
   line = &res->hm_line.resl;
   status = line->status;
   body = HM_OFF2STR(res->hm_body, res);
   send_status = dprintf(conn_fd, res_fmt,
                         line->version, status->code, status->phrase, // line args
                         hdrs_str,                                    // header arg
                         body);                                       // body arg
   if (send_status < 0) {
      perror("dprintf");
      return -1;
   }

   return 0;
}






typedef struct {
   const char *str;
   httpreq_method_t meth;
} hr_str2meth_t;

static hr_str2meth_t hr_str2meth_v[] = {
   {"GET", HR_M_GET},
   {0,            0}
};


httpreq_method_t hr_str2meth(const char *str) {
   for (hr_str2meth_t *it = hr_str2meth_v; it->str; ++it) {
      if (strcmp(it->str, str) == 0) {
         return it->meth;
      }
   }
   return -1;
}

const char * hr_meth2str(httpreq_method_t meth) {
   for (hr_str2meth_t *it = hr_str2meth_v; it->str; ++it) {
      if (meth == it->meth) {
         return it->str;
      }
   }
   return NULL;
}
