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
#include "webserv-multi.h"

#define DOCUMENT_ROOT "/home/nmosier"
#define SERVER_NAME "webserv-multi/1.0"

int main(int argc, char *argv[]) {
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
   httpmsg_t *req;
   if ((req = message_init()) == NULL) {
      perror("message_init");
      exit(2);
   }
   
   int req_stat;
   do {
      req_stat = request_read(server_fd, client_fd, req);
   } while (req_stat == REQ_RD_RAGAIN);

   printf("request_read status: %d\n", req_stat);
   if (req_stat == REQ_RD_RSUCCESS) {
      printf("HTTP REQUEST:\n%s\n", req->hm_text);
   }

   /* parse request */
   int parse_stat = request_parse(req);
   printf("request_parse status: %d\n", parse_stat);
   if (parse_stat == REQ_PRS_RSUCCESS) {
      printf("mode = %d, URI = %s, version = %s\n", req->hm_line.reql.method,
             HM_OFF2STR(req->hm_line.reql.uri, req), HM_OFF2STR(req->hm_line.reql.version, req));
   }

   /* print out headers*/
   printf("HEADERS\n");
   for (httpmsg_header_t *header_it = req->hm_headers;
        header_it->key; ++header_it) {
      printf("%s:\t%s\n", HM_OFF2STR(header_it->key, req), HM_OFF2STR(header_it->value, req));
   }

   httpmsg_t *res;
   if ((res = message_init()) == NULL) {
      perror("message_init");
      exit(3);
   }

   if (response_insert_line(C_FORBIDDEN, "1.1", res) < 0) {
      perror("response_insert_line");
      exit(4);
   }

   if (server_handle_req(client_fd, DOCUMENT_ROOT, SERVER_NAME, req) < 0) {
      perror("server_handle_get");
      exit(7);
   }

   message_delete(req);
   message_delete(res);
   close(client_fd);
   
   exit(0);
}
