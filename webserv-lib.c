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

#define REQ_RD_TEXTFREE(req) ((req)->hr_text_size - ((req)->hr_text_endp - (req)->hr_text))

int request_init(httpreq_t *req) {
   /* zero out struct */
   memset(req, 0, sizeof(httpreq_t));

   /* allocate & initialize buffer */
   req->hr_text_size = REQ_RD_BUFSIZE;
   req->hr_text_endp = req->hr_text = malloc(req->hr_text_size + 1); // +1 for null terminator
   if (req->hr_text == NULL) {
      return -1;
   }
   
   /* allocate & initialize headers */
   req->hr_headers = calloc(REQ_RD_NHEADERS + 1, sizeof(httpreq_header_t)); // +1 for null term.
   if (req->hr_headers == NULL) {
      return -1;
   }
   req->hr_nheaders = REQ_RD_NHEADERS;

   return 0;
}


int request_read(int servsock_fd, int conn_fd, httpreq_t *req) {
   int msg_done;
   ssize_t bytes_received;
   size_t bytes_free;

   /* read until block, EOF, or \r\n */
   msg_done = 0; // whether terminating \r\n has been encountered
   do {
      /* resize text buffer if necessary */
      bytes_free = REQ_RD_TEXTFREE(req);
      if (bytes_free == 0) {
         if (request_resize_text(req->hr_text_size * 2, req) < 0) {
            perror("request_resize_text");
            return REQ_RD_RERROR;
         }
         bytes_free = REQ_RD_TEXTFREE(req); // update free byte count
      }

      /* receive bytes */
      bytes_received = recv(conn_fd, req->hr_text_endp, bytes_free, MSG_DONTWAIT);

      /* if bytes received, update request fields */
      if (bytes_received > 0) {
         /* update text buffer fields */
         req->hr_text_endp += bytes_received;
         *(req->hr_text_endp) = '\0';
         
         /* check for terminating line */
         if (req->hr_text_endp - req->hr_text >= 4 &&
             strcmp("\r\n\r\n", req->hr_text_endp - 4) == 0) {
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


int request_parse(httpreq_t *req) {
   char *saveptr_text;

   ////////// PARSE REQUEST LINE /////////
   char *req_line, *req_method_str, *req_version;
   req_line = strtok_r(req->hr_text, "\n", &saveptr_text); // has trailing '\r'

   /* parse request line method */
   if ((req_method_str = strtok(req_line, " "))) {
      req->hr_line.method = hr_str2meth(req_method_str);
   }
   if (req_method_str == NULL || req->hr_line.method < 0) {
      return REQ_PRS_RSYNTAX;
   }

   /* parse request line URI */
   if ((req->hr_line.uri = strtok(NULL, " ")) == NULL) {
      return REQ_PRS_RSYNTAX;
   }

   /* parse request line HTTP version */
   req_version = strtok(NULL, "\r"); // last item in line
   if (req_version && (req->hr_line.version = strskip("HTTP/", req_version))) {
   } else {
      return REQ_PRS_RSYNTAX;
   }
   
   ///////// PARSE REQUEST HEADERS /////////
   httpreq_header_t *header_it;
   char *header_value;
   char *header_str;
   for (header_it = req->hr_headers;
        (header_str = strtok_r(NULL, "\n", &saveptr_text))
           && strcmp(header_str, "\r");
        ++header_it) {

      /* check if array full */
      if (header_it == req->hr_headers + req->hr_nheaders) {
         size_t header_i = header_it - req->hr_headers; // current index
         size_t new_nheaders = req->hr_nheaders * 2;

         /* expand header size */
         if (request_resize_headers(new_nheaders, req) < 0) {
            return REQ_PRS_RERROR;
         }

         header_it = req->hr_headers + header_i; // update header iterator         
      }
      
      /* parse single request header */

      /* get key */
      if ((header_it->key = strtok(header_str, ":")) == NULL) {
         return REQ_PRS_RSYNTAX;
      }
      /* get value */
      if ((header_value = strtok(NULL, "\r")) == NULL) {
         return REQ_PRS_RSYNTAX;
      } else {
         /* strip leading whitespace */
         header_it->value = strstrip(header_value, " ");
      }
   }
   
   return REQ_PRS_RSUCCESS;
}

void request_delete(httpreq_t *req) {
   if (req) {
      /* free members */
      free(req->hr_headers);
      free(req->hr_text);
      
      /* free request */
      free(req);
   }
}

int request_resize_headers(size_t new_nheaders, httpreq_t *req) {
   httpreq_header_t *newheaders;

   /* reallocate headers array */
   newheaders = realloc(req->hr_headers, (new_nheaders+1) * sizeof(httpreq_header_t));
   if (newheaders == NULL) {
      return -1;
   }
   req->hr_headers = newheaders;
   
   /* zero out uninitialized memory */
   if (new_nheaders > req->hr_nheaders) {
      memset(newheaders + req->hr_nheaders + 1, 0,
             sizeof(httpreq_header_t) * (new_nheaders - req->hr_nheaders));
   }
   req->hr_nheaders = new_nheaders;

   return 0;
}

int request_resize_text(size_t newsize, httpreq_t *req) {
   char *newtext;

   /* reallocate text buffer */
   newtext = realloc(req->hr_text, newsize + 1);
   if (newtext == NULL) {
      return -1;
   }
   req->hr_text_size = newsize;
   req->hr_text_endp += newtext - req->hr_text;
   req->hr_text = newtext;

   return 0;
}
