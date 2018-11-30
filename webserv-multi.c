#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <pthread.h>
#include "webserv-lib.h"
#include "webserv-multi.h"

int main(int argc, char *argv[]) {
   /* test server_listen() */
   int sock_fd;
   if ((sock_fd = server_listen(argv[1], BACKLOG)) < 0) {
      fprintf(stderr, "server_listen failed.\n");
      exit(1);
   }

   printf("sock_fd=%d\n", sock_fd);
   
   exit(0);
}
