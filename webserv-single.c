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
#include "webserv-single.h"

#define DOCUMENT_ROOT "/home/nmosier"
#define SERVER_NAME "webserv-single/1.0"

#define PORT "1024"
#define BACKLOG 10

#define DEBUG 1

int main(int argc, char *argv[]) {
   int optc;
   int optinval;
   const char *optstr = "p:n:";
   const char *port = PORT;
   const char *servname = SERVER_NAME;
      
   /* parse arguments */
   optinval = 0;
   while ((optc = getopt(argc, argv, optstr)) >= 0) {
      switch (optc) {
      case 'p':
         port = optarg;
         break;
      case 'n':
         servname = optarg;
         break;
      default:
         optinval = 1;
         break;
      }
   }
   if (optinval) {
      fprintf(stderr, "%s: [-p port] [-n servname]\n", argv[0]);
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

   if (DEBUG) {
      fprintf(stderr, "entering infinite loop...\n");
   }
   
   /* service new connections & requests (infinite loop) */
   while (1) {
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
                  /* server error*/
                  fprintf(stderr, "server_loop: server socket error\n");
                  if (httpfds_cleanup(&hfds) < 0) {
                     perror("httpfds_cleanup");
                  }
                  return -1;
               } else if (revents & POLLIN) {
                  if (server_accept(fd, &hfds) < 0) {
                     perror("server_accept");
                     if (httpfds_cleanup(&hfds) < 0) {
                        perror("httpfds_cleanup");
                     }
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
                  int req_stat;
                  httpmsg_t *reqp = &hfds.reqs[i];
                  /* read data */
                  req_stat = request_read(servfd, fd, reqp);
                  switch (req_stat) {
                  case REQ_RD_RSUCCESS: {
                     int prs_stat;
                     
                     /* parse request */
                     prs_stat = request_parse(reqp);
                     switch (prs_stat) {
                     case REQ_PRS_RSUCCESS: 
                        if (server_handle_req(fd, DOCUMENT_ROOT, SERVER_NAME, reqp) < 0) {
                           perror("server_handle_req");
                           retv = -1;
                        }
                        break;
                     case REQ_PRS_RSYNTAX:
                        break;
                     case REQ_PRS_RERROR:
                     default:
                        retv = -1;
                        break;
                     }
                     if (httpfds_remove(i, &hfds) < 0) {
                        perror("httpfds_remove");
                        retv = -1;
                     }
                     break;
                  }
                  case REQ_RD_RAGAIN:
                     // still need to read more data
                     break;
                  case REQ_RD_RERROR:
                  default:
                     if (httpfds_remove(i, &hfds) < 0) {
                        perror("httpfds_remove");
                     }
                     retv = -1;
                     break;
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
      return -1;
   }

   return retv;
}

int server_accept(int servfd, httpfds_t *hfds) {
   socklen_t addrlen;
   struct sockaddr_in client_sa;
   int client_fd;

   addrlen = sizeof(client_sa);
   client_fd = accept(servfd, (struct sockaddr *) &client_sa, &addrlen);
   if (client_fd < 0) {
      perror("accept");
      return -1;
   }
   if (httpfds_insert(client_fd, POLLIN, hfds) < 0) {
      perror("httpfds_insert");
      return -1;
   }

   return 0;
}

#define HTTPFDS_MINLEN 16
void httpfds_init(httpfds_t *hfds) {
   hfds->len = 0;
   hfds->count = 0;
   hfds->fds = NULL;
   hfds->reqs = NULL;
}

int httpfds_resize(size_t newlen, httpfds_t *hfds) {
   struct pollfd *fds_tmp;
   httpmsg_t *reqs_tmp;

   if ((fds_tmp = reallocarray(hfds->fds, newlen, sizeof(struct pollfd))) == NULL) {
      return -1;
   }
   hfds->fds = fds_tmp;
   if ((reqs_tmp = reallocarray(hfds->reqs, newlen, sizeof(httpmsg_t))) == NULL) {
      return -1;
   }
   hfds->reqs = reqs_tmp;
   hfds->len = newlen;

   return 0;
}


int httpfds_insert(int fd, int events, httpfds_t *hfds) {
   struct pollfd *fdentry;
   httpmsg_t *reqentry;
   size_t index;

   /* resize if necessary */
   if (hfds->count == hfds->len) {
      size_t newlen;

      newlen = smax(hfds->len * 2, HTTPFDS_MINLEN);
      if (httpfds_resize(newlen, hfds) < 0) {
         return -1;
      }
   }

   index = hfds->count;
   fdentry = &hfds->fds[index];
   reqentry = &hfds->reqs[index];

   /* append fd */
   fdentry->fd = fd;
   fdentry->events = events;

   /* append http request */
   if (message_init(reqentry) < 0) {
      return -1;
   }

   ++hfds->count;
   return 0;
}

/* doesn't change ordering of entries */
int httpfds_remove(size_t index, httpfds_t *hfds) {
   int *fdp = &hfds->fds[index].fd;
   httpmsg_t *reqp = &hfds->reqs[index];
   int retv = 0;
   
   if (close(*fdp) < 0) {
      fprintf(stderr, "close(%d): %s\n", *fdp, strerror(errno));
      retv = -1;
   }
   *fdp = -1; // mark as deleted

   message_delete(reqp);
   return retv;
}

// removes fds < 0
size_t httpfds_pack(httpfds_t *hfds) {
   //struct pollfd *fd_front, *fd_back;
   ssize_t front, back;
   size_t newcount;

   newcount = hfds->count;
   front = 0;
   back = hfds->count - 1;
   while (front < back && hfds->fds[back].fd < 0) {
      --back;
      --newcount;
   }
   while (front < back) {
      if (hfds->fds[front].fd < 0) {
         memcpy(&hfds->fds[front], &hfds->fds[back], sizeof(struct pollfd));
         memcpy(&hfds->reqs[front], &hfds->reqs[back], sizeof(httpmsg_t));
         --back;
         --newcount;
         while (front < back && hfds->fds[back].fd < 0) {
            --back;
            --newcount;
         }
      }
      ++front;
   }

   hfds->count = newcount;
   return newcount;
}

/* cleanup &  delete */
int httpfds_cleanup(httpfds_t *hfds) {
   int retv;

   retv = 0;
   for (size_t i = 0; i < hfds->count; ++i) {
      if (hfds->fds[i].fd >= 0) {
         /* close client socket */
         if (close(hfds->fds[i].fd) < 0) {
            fprintf(stderr, "close(%d): %s\n", hfds->fds[i].fd, strerror(errno));
            retv = -1;
         }
         /* delete request */
         message_delete(&hfds->reqs[i]);
      }
   }
   
   free(hfds->fds);
   free(hfds->reqs);
   hfds->fds = NULL;
   hfds->reqs = NULL;

   return retv;
}
