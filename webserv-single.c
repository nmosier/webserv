#include <stdlib.h>
#include <malloc.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include "webserv-lib.h"
#include "webserv-util.h"
#include "webserv-fds.h"
#include "webserv-dbg.h"
#include "webserv-main.h"
#include "webserv-single.h"


// TODO: make it so it doesn't close servfd
int server_loop(int servfd) {
   httpfds_t hfds;
   int retv;

   retv = 0;
   
   /* initialize client socket list */
   httpfds_init(&hfds);
   
   /* insert server socket to list */
   if (httpfds_insert(servfd, POLLIN, &hfds) < 0) {
      perror("httpfds_insert");
      if (httpfds_cleanup(&hfds) < 0) {
         perror("httpfds_cleanup");
      }
      return -1;
   }

   /* service new connections & requests (infinite loop) */
   while (retv >= 0) {
      int nready;

      if (DEBUG) {
         fprintf(stderr, "polling...\n");
      }
      
      if ((nready = poll(hfds.fds, hfds.count, -1)) < 0) {
         perror("poll");
         if (httpfds_cleanup(&hfds) < 0) {
            perror("httpfds_cleanup");
         }
         return -1;
      }

      if (DEBUG) {
         fprintf(stderr, "poll: %d descriptors ready\n", nready);
      }
      
      for (size_t i = 0; nready > 0; ++i) {
         int fd;
         int revents;
         
         fd = hfds.fds[i].fd;
         revents = hfds.fds[i].revents;
         if (fd >= 0 && revents) {
            if (fd == servfd) {
               if (revents & POLLERR) {
                  /* server error */
                  fprintf(stderr, "server_loop: server socket error\n");
                  if (httpfds_cleanup(&hfds) < 0) {
                     perror("httpfds_cleanup");
                  }
                  return -1;
               } else if (revents & POLLIN) {
                  int new_client_fd;
                  
                  /* accept new connection */
                  if ((new_client_fd = server_accept(fd)) < 0) {
                     perror("server_accept");
                     if (httpfds_cleanup(&hfds) < 0) {
                        perror("httpfds_cleanup");
                     }
                     return -1;
                  }
                  /* add new connection to list */
                  if (httpfds_insert(new_client_fd, POLLIN, &hfds) < 0) {
                     perror("httpfds_insert");
                     return -1;
                  }
               }
            } else {
               if (revents & POLLERR) {
                  /* close client socket & mark as closed */
                  if (httpfds_remove(i, &hfds) < 0) {
                     perror("httpds_remove");
                     retv = -1;
                  }
               } else if (revents & POLLIN) {
                  int rm_fd;
                  httpmsg_t *reqp;

                  /* initialize variables */
                  reqp = &hfds.reqs[i];
                  rm_fd = 0; // don't remove fd by default
                  
                  /* read data */
                  if (request_read(fd, reqp) < 0) {
                     /* incomplete read */
                     if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        /* fatal error (unrelated to blocking) */
                        perror("request_read");
                        rm_fd = 1;
                        retv = -1;
                     }
                  } else {
                     /* parse complete request */
                     if (request_parse(reqp) < 0) {
                        perror("request_parse");
                        rm_fd = 1;
                        if (errno != EBADMSG) {
                           retv = -1; // internal error
                        }
                     } else {
                        if (server_handle_req(fd, DOCUMENT_ROOT, SERVER_NAME, reqp) < 0) {
                           perror("server_handle_req");
                           retv = -1;
                        }
                        rm_fd = 1; // done receiving data
                     }
                  }
                  
                  if (rm_fd && httpfds_remove(i, &hfds) < 0) {
                     perror("httpfds_remove");
                     retv = -1;
                  }

               }
            }
            
            --nready;
         }
         
      }

      /* pack httpfds in case some connections were closed */
      httpfds_pack(&hfds); // never fails
   }
   
   if (httpfds_cleanup(&hfds) < 0) {
      perror("httpfds_cleanup");
      retv = -1;
   }

   return retv;
}


void server_handle_event(size_t fd_index, httpfds_t *hfds) {

}
