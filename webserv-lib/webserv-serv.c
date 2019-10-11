#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>
#include <fcntl.h>
#include <sys/utsname.h>
#include <netinet/in.h>
//#include "webserv-lib.h"
#include "webserv-util.h"
#include "webserv-dbg.h"
#include "webserv-serv.h"
#include "webserv-req.h"
#include "webserv-res.h"

/* server_start()
 * DESC: start the web server on port _port_ with backlog _backlog_.
 * RETV: the server socket on success, -1 on error.
 */
int server_start(const char *port, int backlog) {
   int servsock_fd;
   struct addrinfo *res;
   int gai_stat;
   int error;

   /* initialize variables */
   servsock_fd = -1;
   res = NULL;
   error = 0; // error=1 if error occurred
   
   /* obtain socket */
   if ((servsock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
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


/* server_accept
 * DESC: accept client connection.
 * ARGS:
 *  - servfd: server socket listening for connections.
 * RETV: see accept(2).
 * NOTE: blocks.
 */
int server_accept(int servfd) {
   socklen_t addrlen;
   struct sockaddr_in client_sa;
   int client_fd;

   /* accept socket */
   addrlen = sizeof(client_sa);
   client_fd = accept(servfd, (struct sockaddr *) &client_sa, &addrlen);

   return client_fd;
}


/* server_handle_req()
 * DESC: given HTTP request that has been fully read & parsed, create HTTP response.
 * ARGS:
 *  - conn_fd: client socket to send response to.
 *  - docroot: the root directory to prepend resource requests to.
 *  - servname: name of server version.
 *  - req: pointer to request.
 *  - res: pointer to response to be created.
 * RETV: 0 on success, -1 on error.
 * ERRS:
 *  - EBADRQC: bad HTTP method in request.
 *  - see server_handle_get()
 */
int server_handle_req(int conn_fd, const char *docroot, const char *servname,
                      httpmsg_t *req, httpmsg_t *res, const filetype_table_t *ftypes) {
   switch (req->hm_line.reql.method) {
   case M_GET:
      return server_handle_get(conn_fd, docroot, servname, req, res, ftypes);
   default:
      errno = EBADRQC;
      return -1;
   }
}


// handle GET request
// TODO: make case statement table-driven, not switch case
/* server_handle_get()
 * DESC: given HTTP request with method "GET", create HTTP response.
 * ARGS: (see server_handle_req())
 * RETV: 0 on success, -1 on error.
 * ERRS: (see server_handle_req())
 */
int server_handle_get(int conn_fd, const char *docroot, const char *servname, httpmsg_t *req,
                      httpmsg_t *res, const filetype_table_t *ftypes) {
   char *path;
   int code;

   /* create response */
   response_init(res);
   
   /* get response code & full path */
   if ((code = request_document_find(docroot, &path, req)) < 0) {
      response_delete(res);
      return -1;
   }
      
   /* insert file */
   if (code == C_OK) {
      if (response_insert_file(path, res, ftypes) < 0) {
         response_delete(res);
         free(path);
         return -1;
      }
      free(path);
   } else {
      const char *body;
      switch (code) {
      case C_NOTFOUND:
         body = C_NOTFOUND_BODY;
         break;
      case C_FORBIDDEN:
         body = C_FORBIDDEN_BODY;
         break;
      default:
         response_delete(res);
         errno = EBADRQC;
         return -1;
      }
      // note: strlen(body)+1 causes file to be downloaded?
      if (response_insert_body(body, strlen(body), CONTENT_TYPE_PLAIN, res) < 0) {
         response_delete(res);
         return -1;
      }
   }

   /* insert general headers */
   if (response_insert_genhdrs(res) < 0) {
      response_delete(res);
      return -1;
   }

   /* insert server headers */
   if (response_insert_servhdrs(servname, res) < 0) {
      response_delete(res);
      return -1;
   }
   
   /* set response line */
   if (response_insert_line(code, HM_HTTP_VERSION, res) < 0) {
      response_delete(res);
      return -1;
   }
   
   return 0;
}
