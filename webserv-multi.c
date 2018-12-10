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
#include "webserv-multi.h"

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
   httpmsg_t req;
   int req_stat, prs_stat;
   int64_t retv; // sizeof(long long) == sizeof(void *)

   /* initialize variables */
   thd_args = (struct client_thread_args *) args;
   client_fd = thd_args->client_fd;
   retv = -1; // 0 is success
   if (message_init(&req) < 0) {
      perror("message_init");
      return (void *) -1;
   }

   /* read request */
   if (message_init(&req) < 0) {
      perror("message_init");
      goto cleanup;
   }
   do {
      req_stat = request_read(client_fd, &req);
   } while (req_stat == REQ_RD_RAGAIN);
   if (req_stat == REQ_RD_RERROR) {
      perror("request_read");
      goto cleanup;
   }

   /* parse request */
   prs_stat = request_parse(&req);
   if (prs_stat != REQ_PRS_RSUCCESS) {
      /* print error if internal error */
      if (prs_stat == REQ_PRS_RERROR) {
         perror("request_parse");
      }
      goto cleanup;
   }

   /* handle request */
   if (server_handle_req(client_fd, DOCUMENT_ROOT, SERVER_NAME, &req) < 0) {
      perror("server_handle_get");
      goto cleanup;
   }

   retv = 0;
   
 cleanup:
   if (close(client_fd) < 0) {
      perror("close");
      retv = -1;
   }
   message_delete(&req);

   return (void *) retv;
}
      



int main_(int argc, char *argv[]) {
   /* test server_start() */
   int server_fd;
   if ((server_fd = server_start(argv[1], BACKLOG)) < 0) {
      fprintf(stderr, "server_start failed.\n");
      exit(1);
   }

   printf("server_fd=%d\n", server_fd);
   
   /* accept connection */
   struct sockaddr_in client_sa;
   socklen_t addrlen;
   int client_fd;


   /* try polling! */
   struct pollfd servpfd;
   servpfd.fd = server_fd;
   servpfd.events = POLLIN;
   int pollval = poll(&servpfd, 1, -1);
   if (pollval < 0) {
      perror("poll");
      exit(10);
   }
   if (pollval > 0) {
      fprintf(stderr, "poll: events = %d\n", servpfd.revents);
   }
   fprintf(stderr, "here\n");
   
   do {
      addrlen = sizeof(client_sa);
      client_fd = accept(server_fd, (struct sockaddr *) &client_sa, &addrlen);
   } while (client_fd < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));

   
   printf("client_fd=%d\n", client_fd);

   /* get HTTP request */
   httpmsg_t req;
   if (message_init(&req) < 0) {
      perror("message_init");
      exit(2);
   }
   
   int req_stat;
   do {
      req_stat = request_read(client_fd, &req);
   } while (req_stat == REQ_RD_RAGAIN);

   printf("request_read status: %d\n", req_stat);
   if (req_stat == REQ_RD_RSUCCESS) {
      printf("HTTP REQUEST:\n%s\n", req.hm_text);
   }

   /* parse request */
   int parse_stat = request_parse(&req);
   printf("request_parse status: %d\n", parse_stat);
   if (parse_stat == REQ_PRS_RSUCCESS) {
      printf("mode = %d, URI = %s, version = %s\n", req.hm_line.reql.method,
             HM_OFF2STR(req.hm_line.reql.uri, &req), HM_OFF2STR(req.hm_line.reql.version, &req));
   }

   /* print out headers*/
   printf("HEADERS\n");
   for (httpmsg_header_t *header_it = req.hm_headers;
        header_it->key; ++header_it) {
      printf("%s:\t%s\n", HM_OFF2STR(header_it->key, &req), HM_OFF2STR(header_it->value, &req));
   }

   httpmsg_t res;
   if (message_init(&res) < 0) {
      perror("message_init");
      exit(3);
   }

   if (response_insert_line(C_FORBIDDEN, "1.1", &res) < 0) {
      perror("response_insert_line");
      exit(4);
   }

   if (server_handle_req(client_fd, DOCUMENT_ROOT, SERVER_NAME, &req) < 0) {
      perror("server_handle_get");
      exit(7);
   }

   message_delete(&req);
   message_delete(&res);
   close(client_fd);
   
   exit(0);
}
