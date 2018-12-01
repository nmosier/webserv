#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "webserv-lib.h"

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
#define REQ_RD_BUFSIZE  (0x1000-1)
#define REQ_RD_NHEADERS 10

#define REQ_RD_TEXTFREE(req) ((req)->hr_text_size - ((req)->hr_text_endp - (req)->hr_text))

int request_read(int servsock_fd, int conn_fd, httpreq_t **reqp) {
   httpreq_t *req;
   char *reqbuf;
   size_t reqbuf_size;
   int msg_done;
   ssize_t bytes_received;
   size_t bytes_free;

   
   req = *reqp;
   if (req == NULL) {
      /* initialize HTTP request & buffer  */

      /* allocate & initialize request */
      req = *reqp = malloc(sizeof(httpreq_t));
      if (req == NULL) {
         perror("malloc");
         return REQ_RD_RERROR;
      }
      memset(req, 0, sizeof(httpreq_t)); // zero out request (for error handling)

      /* allocate & initialize buffer */
      reqbuf_size = REQ_RD_BUFSIZE;
      reqbuf = malloc(reqbuf_size + 1); // +1 for null term.
      if (reqbuf == NULL) {
         perror("malloc");
         request_delete(req);
         *reqp = NULL;
         return REQ_RD_RERROR;
      }
      *reqbuf = '\0'; // reqbuf points to empty string
      req->hr_text = reqbuf;
      req->hr_text_endp = reqbuf;
      req->hr_text_size = reqbuf_size;

      /* allocate & initialize headers */
      req->hr_headers = calloc(REQ_RD_NHEADERS + 1, sizeof(httpreq_header_t)); // +1 for null term.
      if (req->hr_headers == NULL) {
         perror("malloc");
         request_delete(req);
         *reqp = NULL;
         return REQ_RD_RERROR;
      }
   }

   /* read until block, EOF, or \r\n */
   msg_done = 0; // whether terminating \r\n has been encountered
   do {
      /* resize text buffer if necessary */
      bytes_free = REQ_RD_TEXTFREE(req);
      if (bytes_free == 0) {
         char *newtext;
         newtext = realloc(req->hr_text, req->hr_text_size * 2);
         if (newtext == NULL) {
            perror("realloc");
            request_delete(req);
            *reqp = NULL;
            return REQ_RD_RERROR;
         }
         req->hr_text_size *= 2;
         req->hr_text_endp += newtext - req->hr_text;
         req->hr_text = newtext;
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
      request_delete(req);
      *reqp = NULL;
      return REQ_RD_RERROR;
   }
   
   /* check for premature EOF */
   if (bytes_received == 0 && !msg_done) {
      request_delete(req);
      *reqp = NULL;
      return REQ_RD_RSYNTAX;
   }

   /* otherwise, parse request */
   printf("HTTP REQUEST from %d:\n%s\n", conn_fd, req->hr_text);
   request_delete(req);
   *reqp = NULL;

   return REQ_RD_RSUCCESS;
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
