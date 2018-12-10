#include <stdlib.h>
#include <malloc.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <poll.h>
#include "webserv-lib.h"
#include "webserv-util.h"
#include "webserv-fds.h"
#include "webserv-dbg.h"

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

/* cleanup & delete */
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
