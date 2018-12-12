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
   memset(hfds, 0, sizeof(httpfds_t));
}

int httpfds_resize(size_t newlen, httpfds_t *hfds) {
   struct pollfd *fds_tmp;
   httpmsg_t *reqs_tmp, *resps_tmp;

   if ((fds_tmp = reallocarray(hfds->fds, newlen, sizeof(struct pollfd))) == NULL) {
      return -1;
   }
   hfds->fds = fds_tmp;
   
   if ((reqs_tmp = reallocarray(hfds->reqs, newlen, sizeof(httpmsg_t))) == NULL) {
      return -1;
   }
   hfds->reqs = reqs_tmp;
   
   if ((resps_tmp = reallocarray(hfds->resps, newlen, sizeof(httpmsg_t))) == NULL) {
      return -1;
   }
   hfds->resps = resps_tmp;

   hfds->len = newlen;

   return 0;
}


int httpfds_insert(int fd, int events, httpfds_t *hfds) {
   struct pollfd *fdentry;
   httpmsg_t *reqentry, *respentry;
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
   respentry = &hfds->resps[index];

   /* initialize entry */
   fdentry->fd = fd;
   fdentry->events = events;
   request_init(reqentry);
   response_init(respentry);

   ++hfds->count;
   
   return 0;
}

/* doesn't change ordering of entries */
int httpfds_remove(size_t index, httpfds_t *hfds) {
   int *fdp;
   httpmsg_t *reqp, *resp;
   int retv;

   /* initialize vars */
   fdp = &hfds->fds[index].fd;
   reqp = &hfds->reqs[index];
   resp = &hfds->resps[index];
   retv = 0;

   if (*fdp >= 0) {
      /* close socket & delete response & request */
      if (close(*fdp) < 0) {
         fprintf(stderr, "close(%d): %s\n", *fdp, strerror(errno));
         retv = -1;
      }
      *fdp = -1; // mark as deleted
      request_delete(reqp);
      response_delete(resp);
   }
   
   return retv;
}

// removes fds < 0
size_t httpfds_pack(httpfds_t *hfds) {
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
         memcpy(&hfds->resps[front], &hfds->resps[back], sizeof(httpmsg_t));

         do {
            --back;
            --newcount;
         } while (front < back && hfds->fds[back].fd < 0);
         //--back;
         //--newcount;
         //}
      }
      
      ++front;
   }

   hfds->count = newcount;
   
   return newcount;
}

/* cleanup & delete */
int httpfds_delete(httpfds_t *hfds) {
   int retv;

   retv = 0;
   for (size_t i = 0; i < hfds->count; ++i) {
      httpfds_remove(i, hfds);
   }
   
   free(hfds->fds);
   free(hfds->reqs);
   free(hfds->resps);
   memset(hfds, 0, sizeof(httpfds_t));

   return retv;
}
