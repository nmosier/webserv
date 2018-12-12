#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include "webserv-lib.h"
#include "webserv-dbg.h"
#include "webserv-main.h"

/* types */
struct client_thread_args {
   int client_fd;
};

/* prototypes */
void *client_loop(void *args);

int server_loop(int servfd) {
   int retv;

   /* initialize variables */
   retv = 0;

   /* accept new connections & spin off new threads */
   while (1) {
      int client_fd;
      pthread_t thd;
      struct client_thread_args thd_args;

      /* accept new connection */
      if ((client_fd = server_accept(servfd)) < 0) {
         perror("server_accept");
         retv = -1;
         goto cleanup;
      }

      /* spin off new thread */
      thd_args.client_fd = client_fd;
      if (pthread_create(&thd, NULL, client_loop, &thd_args)) {
         /* don't exit -- wait for other threads to die */
         perror("pthread_create");
         retv = -1;
      }
   }
      
 cleanup:
   if (close(servfd) < 0) {
      perror("close");
      retv = -1;
   }
   
   return retv;
}


void *client_loop(void *args) {
   struct client_thread_args *thd_args;
   int client_fd;
   httpmsg_t req, res;
   int msg_stat;
   void *retv;

   /* initialize variables */
   thd_args = (struct client_thread_args *) args;
   client_fd = thd_args->client_fd;
   retv = (void *) -1; // error by default
   request_init(&req);
   response_init(&res);

   /* read request to completion */
   while ((msg_stat = request_read(client_fd, &req)) < 0
          && (errno == EAGAIN || errno == EWOULDBLOCK)) {}
   if (msg_stat < 0) {
      perror("request_read");
      goto cleanup;
   }

   /* parse request */
   if (request_parse(&req) < 0) {
      perror("request_parse");
      goto cleanup;
   }
   
   /* create response */
   if (server_handle_req(client_fd, DOCUMENT_ROOT, SERVER_NAME, &req, &res) < 0) {
      perror("server_handle_get");
      goto cleanup;
   }

   /* send response */
   while ((msg_stat = response_send(client_fd, &res)) < 0
          && (errno == EAGAIN || errno == EWOULDBLOCK)) {}
   if (msg_stat < 0) {
      perror("request_send");
      goto cleanup;
   }

   retv = (void *) 0;
   
 cleanup:
   if (close(client_fd) < 0) {
      perror("close");
      retv = (void *) -1;
   }
   request_delete(&req);
   response_delete(&res);

   return retv;
}
