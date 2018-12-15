#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include "webserv-lib.h"
#include "webserv-util.h"
#include "webserv-dbg.h"
#include "webserv-main.h"

int server_accepting = 0; // whether server is accepting new connections

int main(int argc, char *argv[]) {
   int optc;
   int optinval;
   const char *optstr = "p:t:";
   const char *port = PORT;
   const char *types_path = CONTENT_TYPES_PATH;
   
   /* parse arguments */
   optinval = 0;
   while ((optc = getopt(argc, argv, optstr)) >= 0) {
      switch (optc) {
      case 'p':
         port = optarg;
         break;
      case 't':
         types_path = optarg;
      default:
         optinval = 1;
         break;
      }
   }
   if (optinval) {
      fprintf(stderr, "%s: [-p port]\n", argv[0]);
      exit(1);
   }

   /* install signal handlers */
   struct sigaction sa;
   
   /* install SIGINT handler */
   memset(&sa, 0, sizeof(sa));
   sa.sa_handler = handler_sigint;
   sigemptyset(&sa.sa_mask);
   sa.sa_flags = 0;
   if (sigaction(SIGINT, &sa, NULL) < 0) {
      perror("sigaction");
      exit(2);
   }
   
   /* install SIGPIPE handler (implementation-dependent) */
   sa.sa_handler = handler_sigpipe;
   if (sigaction(SIGPIPE, &sa, NULL) < 0) {
      perror("sigaction");
      exit(2);
   }

   /* load content types table */
   filetype_table_t typetab;
   if (content_types_init(types_path, &typetab) < 0) {
      perror("content_types_init");
      exit(3);
   }
   
   /* start web server */
   int servfd;
   if ((servfd = server_start(port, BACKLOG)) < 0) {
      fprintf(stderr, "%s: failed to start server; exiting.\n", argv[0]);
      exit(4);
   }
   server_accepting = 1;

   /* run server loop */
   if (server_loop(servfd) < 0) {
      fprintf(stderr, "%s: internal error occurred; exiting.\n", argv[0]);
      if (close(servfd) < 0) {
         perror("close");
      }
      exit(5);
   }

   if (close(servfd) < 0) {
      perror("close");
      exit(6);
   }
   
   exit(0);
}

void handler_sigint(int signum) {
   /* stop accepting new connections */
   printf("webserv-single: closing server to new connections...\n");
   server_accepting = 0;
}

void handler_sigpipe(int signum) {
   /* catch SIGPIPE & do nothing so that send(2) will fail with
    * EPIPE in the corresponding thread */
   if (DEBUG) {
      printf("webserv-multi: caught signal SIGPIPE\n");
   }
}
