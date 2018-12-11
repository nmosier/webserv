#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "webserv-lib.h"
#include "webserv-dbg.h"
#include "webserv-main.h"

int main(int argc, char *argv[]) {
   int optc;
   int optinval;
   const char *optstr = "p:";
   const char *port = PORT;
      
   /* parse arguments */
   optinval = 0;
   while ((optc = getopt(argc, argv, optstr)) >= 0) {
      switch (optc) {
      case 'p':
         port = optarg;
         break;
      default:
         optinval = 1;
         break;
      }
   }
   if (optinval) {
      fprintf(stderr, "%s: [-p port]\n", argv[0]);
      exit(1);
   }
   
   /* start web server */
   int servfd;
   if ((servfd = server_start(port, BACKLOG)) < 0) {
      fprintf(stderr, "%s: failed to start server; exiting.\n", argv[0]);
      exit(2);
   }
   
   if (server_loop(servfd) < 0) {
      fprintf(stderr, "%s: internal error occurred; exiting.\n", argv[0]);
      exit(3);
   }
   
   exit(0);
}
