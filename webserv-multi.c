#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include "webserv-lib.h"
#include "webserv-multi.h"

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
   
   do {
      addrlen = sizeof(client_sa);
      client_fd = accept(server_fd, (struct sockaddr *) &client_sa, &addrlen);
   } while (client_fd < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
   
   printf("client_fd=%d\n", client_fd);

   /* get HTTP request */
   httpreq_t *req = NULL; // must initialize
   int req_stat;
   do {
      req_stat = request_get(server_fd, client_fd, &req);
   } while (req_stat == REQ_RD_RAGAIN);

   printf("request_read status: %d\n", req_stat);
   
   exit(0);
}
