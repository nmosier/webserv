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

int handle_pollevents_server(int servfd, int revents, httpfds_t *hfds);
int handle_pollevents_client(int clientfd, int index, int revents, httpfds_t *hfds);

int server_loop(int servfd) {
   httpfds_t hfds;
   int retv;

   retv = 0;
   
   /* initialize client socket list */
   httpfds_init(&hfds);
   
   /* insert server socket to list */
   if (httpfds_insert(servfd, POLLIN, &hfds) < 0) {
      perror("httpfds_insert");
      if (httpfds_delete(&hfds) < 0) {
         perror("httpfds_delete");
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
         if (httpfds_delete(&hfds) < 0) {
            perror("httpfds_delete");
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
               if (handle_pollevents_server(fd, revents, &hfds) < 0) {
                  return -1;
               }
            } else {
               if (handle_pollevents_client(fd, i, revents, &hfds) < 0) {
                  return -1;
               }
            }
            
            --nready;
         }
         
      }

      /* pack httpfds in case some connections were closed */
      httpfds_pack(&hfds); // never fails
   }
   
   if (httpfds_delete(&hfds) < 0) {
      perror("httpfds_delete");
      retv = -1;
   }

   return retv;
}


int handle_pollevents_server(int servfd, int revents, httpfds_t *hfds) {
   if (revents & POLLERR) {
      /* server error */
      fprintf(stderr, "server_loop: server socket error\n");
      if (httpfds_delete(hfds) < 0) {
         perror("httpfds_delete");
      }
      
      return -1;
   } else if (revents & POLLIN) {
      int new_client_fd;
      
      /* accept new connection */
      if ((new_client_fd = server_accept(servfd)) < 0) {
         perror("server_accept");
         if (httpfds_delete(hfds) < 0) {
            perror("httpfds_delete");
         }
         return -1;
      }
      
      /* add new connection to list */
      if (httpfds_insert(new_client_fd, POLLIN, hfds) < 0) {
         perror("httpfds_insert");
         return -1;
      }
   }

   return 0;
}


int handle_pollevents_client(int clientfd, int index, int revents, httpfds_t *hfds) {
   int retv = 0;

   if (revents & POLLERR) {
      /* close client socket & mark as closed */
      if (httpfds_remove(index, hfds) < 0) {
         perror("httpds_remove");
         retv = -1;
      }
   } else if (revents & POLLIN) {
      httpmsg_t *reqp, *resp;
      
      /* initialize variables */
      reqp = &hfds->reqs[index];
      resp = &hfds->resps[index];
      
      /* read data */
      if (request_read(clientfd, reqp) < 0) {
         /* incomplete read -- check if due to nonblocking */
         if (errno != EAGAIN && errno != EWOULDBLOCK) {
            /* fatal error (unrelated to blocking) */
            perror("request_read");
            if (httpfds_remove(index, hfds) < 0) {
               perror("httpfds_remove");
            }
            retv = -1;
         }
      } else {
         /* finished reading request */
         /* parse complete request */
         if (request_parse(reqp) < 0) {
            /* parser error */
            perror("request_parse");
            if (httpfds_remove(index, hfds) < 0) {
               perror("httpfds_remove");
               retv = -1;
            }
            if (errno != EBADMSG) {
               retv = -1; // internal error
            }
         } else {
            /* successfully parse request */
            /* create response for request */
            if (server_handle_req(clientfd, DOCUMENT_ROOT, SERVER_NAME, reqp, resp) < 0) {
               perror("server_handle_req");
               retv = -1;
            }

            /* mark pollfd as ready to receive data */
            hfds->fds[index].events = POLLOUT;
         }
      }
   } else if (revents & POLLOUT) {
      /* send response */
      if (response_send(clientfd, &hfds->resps[index]) < 0) {
         /* incomplete write -- check if due to nonblocking */
         if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("response_send");
            return -1;
         }
      } else {
         /* sending completed -- can remove httpfd */
         if (httpfds_remove(index, hfds) < 0) {
            perror("httpfds_remove");
            return -1;
         }
      }
   }

   return retv;
}
