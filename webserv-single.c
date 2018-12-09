#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#define DOCUMENT_ROOT "/home/nmosier"
#define SERVER_NAME "webserv-single/1.0"

#define PORT "1024"
#define BACKLOG 10

int pollfds_init(pollfds_t *pfds);
int pollfds_resize(size_t newlen, pollfds_t *pfds);
int pollfds_insert(int fd, int events, pollfds_t *pfds);
ssize_t pollfds_pack(size_t index, pollfds_t *pfds);
int pollfds_cleanup(pollfds_t *pfds);

int server_loop();

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

   if (server_loop() < 0) {
      fprintf(stderr, "%s: internal error occurred; exiting.\n", argv[0]);
      exit(3);
   }


   
   do {
      addrlen = sizeof(client_sa);
      client_fd = accept(server_fd, (struct sockaddr *) &client_sa, &addrlen);
   } while (client_fd < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));

   
   exit(0);
}

typedef struct {
   struct pollfd *fds;
   size_t len; // length of allocated array
   size_t count; // number of fds currently in array
} pollfds_t;

int server_loop(int servfd) {
   pollfds_t pfds;
   socklen_t addrlen;

   /* initialize client socket list */
   if (pollfds_init(&pfds) < 0) {
      perror("pollfds_init");
      return -1;
   }
   
   /* insert server socket to list */
   if ((serv_i = pollfds_insert(servfd, POLLIN, &pfds)) < 0) {
      perror("pollfds_insert");
      goto cleanup;
   }

   /* service new connections & requests (infinite loop) */
   while (1) {
      int nready;

      if ((nready = poll(pfds->fds, pfds->count, -1)) < 0) {
         perror("poll");
         goto cleanup;
      }
      if (nready > 0) {
         int serv_revents, client_revents;
         // TODO: for loop
      }
      
   }

   /* cleanup */
 cleanup:
   /* close all client sockets */
   for (struct pollfd *client_pfd = client_pfds; client_pfd < client_pfds_end; ++client_pfd) {
      if (close(client_pfd->fd) < 0) {
         fprintf("close(%d): %s\n", client_pfd->fd, strerror(errno));
      }
   }
   free(client_pfds);

   return -1;
}

int server_accept(int servfd, pollfds_t *pfds) {
   socklen_t addrlen;
   struct sockaddr_in client_sa;
   int client_fd;

   addrlen = sizeof(client_sa);
   client_fd = accept(servfd, (struct sockaddr *) &client_sa, &addrlen);
   if (client_fd < 0) {
      perror("accept");
      return -1;
   }
   if (pollfds_insert(client_fd, POLLIN, pfds) < 0) {
      perror("pollfds_insert");
      return -1;
   }

   return 0;
}

#define POLLFDS_INIT_LEN
int pollfds_init(pollfds_t *pfds) {
   pfds->len = POLLFDS_INIT_LEN;
   pfds->count = 0;
   if ((pfds->fds = calloc(pfds->len, sizeof(struct pollfd))) < 0) {
      return -1;
   }

   return 0;
}

int pollfds_resize(size_t newlen, pollfds_t *pfds) {
   struct pollfd *fds_tmp;

   if ((fds_tmp = reallocarray(pfds->fds, newlen, sizeof(struct pollfd))) == NULL) {
      return -1;
   }
   pfds->fds = fds_tmp;
   pfds->len = newlen;

   return 0;
}


int pollfds_insert(int fd, int events, pollfds_t *pfds) {
   struct pollfd *newentry;
   
   /* resize if necessary */
   if (pfds->count == pfds->len) {
      if (pollfds_resize(pfds->len * 2, pfds) < 0) {
         return -1;
      }
   }

   /* append fd */
   newentry = pfds->fds + pfds->count;
   newentry->fd = fd;
   newentry->events = events;

   return 0;
}

// no remove() function, since would disrupt iterators
// removes fds < 0
ssize_t pollfds_pack(size_t index, pollfds_t *pfds) {
   struct pollfd *fd_front, *fd_back;
   size_t newcount;

   newcount = pfds->count;
   fd_front = pfds->fds;
   fd_back = pfds->fds + pfds->count - 1;
   while (fd_front < fd_back && fd_back->fd < 0) {
      --fd_back;
      --newcount;
   }
   while (fd_front < fd_back) {
      if (fd_front->fd < 0) {
         memcpy(fd_front, fd_back, sizeof(struct pollfd));
         --fd_back;
         --newcount;
         while (fd_front < fd_back && fd_back->fd < 0) {
            --fd_back;
            --newcount;
         }
      }
      ++fd_front;
   }

   pfds->count = newcount;
   return newcount;
}

/* cleanup */
int pollfds_cleanup(pollfds_t *pfds) {
   int retv;

   retv = 0;
   for (struct pollfd *pfd = pfds->fds; pfd < pfds->fds + pfds->count; ++pfd) {
      if (pfd->fd >= 0) {
         if (close(pfd->fd) < 0) {
            fprintf("close(%d): %s\n", pfd->fd, strerror(errno));
            retv = -1;
         }
      }
   }
   
   free(pfds->fds);

   return retv;
}
