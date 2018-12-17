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

/* main()
 * NOTE: this main method is shared between webserv-multi and webserv-single. main() performs setup &
 *       cleanup and calls server_loop(), which is implementation-specific.
 */
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
   
   /* install SIGPIPE handler */
   sa.sa_handler = handler_sigpipe;
   if (sigaction(SIGPIPE, &sa, NULL) < 0) {
      perror("sigaction");
      exit(2);
   }

   /* load content types table */
   filetype_table_t typetab;
   if (content_types_load(types_path, &typetab) < 0) {
      perror("content_types_init");
      exit(3);
   }

   /* save parsed & sorted content type table */
   if (DEBUG) {
      if (content_types_save("mime_sorted.types", &typetab) < 0) {
         perror("content_types_save");
         exit(4);
      }
   }
   
   /* start web server */
   int servfd, exitno;
   if ((servfd = server_start(port, BACKLOG)) < 0) {
      fprintf(stderr, "%s: failed to start server; exiting.\n", argv[0]);
      exit(5);
   }
   server_accepting = 1;

   /* run server loop */
   exitno = 0;
   if (server_loop(servfd, &typetab) < 0) {
      fprintf(stderr, "%s: internal error occurred; exiting.\n", argv[0]);
      exitno = 6;
   }

   /* cleanup */
   if (close(servfd) < 0) {
      perror("close");
      exitno = 7;
   }
   content_types_delete(&typetab);
   
   exit(exitno);
}

/* handler_sigint()
 * DESC: catches the SIGINT signal and tells the server to stop accepting new connections.
 */
void handler_sigint(int signum) {
   /* stop accepting new connections */
   printf("webserv-single: closing server to new connections...\n");
   server_accepting = 0;
}

/* handler_sigpipe()
 * DESC: catches the SIGPIPE signal. Normally, a write to a broken pipe or socket triggers this,
 *       terminating the program. Catching this and doing nothing allows the multithreaded web 
 *       server to handle it within the main code body.
 * NOTE: webserv-single should never receive this signal, since it uses poll(2) to determine if
 *       any file descriptor errors occurred.
 */
void handler_sigpipe(int signum) {
   /* catch SIGPIPE & do nothing so that send(2) will fail with
    * EPIPE in the corresponding thread */
   if (DEBUG) {
      printf("webserv-multi: caught signal SIGPIPE\n");
   }
}
