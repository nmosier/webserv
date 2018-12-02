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
   httpreq_t *req = malloc(sizeof(httpreq_t));
   if (req == NULL) {
      perror("malloc");
      exit(1);
   }
   if (request_init(req) < 0) {
      perror("request_init");
      exit(2);
   }
   
   int req_stat;
   do {
      req_stat = request_read(server_fd, client_fd, req);
   } while (req_stat == REQ_RD_RAGAIN);

   printf("request_read status: %d\n", req_stat);
   if (req_stat == REQ_RD_RSUCCESS) {
      printf("HTTP REQUEST:\n%s\n", req->hr_text);
   }

   /* parse request */
   int parse_stat = request_parse(req);
   printf("request_parse status: %d\n", parse_stat);
   if (parse_stat == REQ_PRS_RSUCCESS) {
      printf("mode = %d, URI = %s, version = %s\n", req->hr_line.method,
             req->hr_line.uri, req->hr_line.version);
   }

   /* print out headers*/
   printf("HEADERS\n");
   for (httpreq_header_t *header_it = req->hr_headers;
        header_it->key; ++header_it) {
      printf("%s:\t%s\n", header_it->key, header_it->value);
   }

   request_delete(req);
   
   exit(0);
}
