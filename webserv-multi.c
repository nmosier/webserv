#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <pthread.h>
#include "webserv-lib.h"
#include "webserv-multi.h"

int main(int argc, char *argv[]) {
   /* test server_start() */
   int sock_fd;
   if ((sock_fd = server_start(argv[1], BACKLOG)) < 0) {
      fprintf(stderr, "server_start failed.\n");
      exit(1);
   }

   printf("sock_fd=%d\n", sock_fd);
   
   exit(0);
}
