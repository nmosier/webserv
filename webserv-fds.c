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

/* httpfds_init()
 * DESC: initializes a list of HTTP file descriptors ("hfds").
 */
void httpfds_init(httpfds_t *hfds) {
   memset(hfds, 0, sizeof(httpfds_t));
}

/* httpfds_resize()
 * DESC: resizes all 3 arrays (fds, reqs, resps) to length _newlen_. If the number
 *       of elements before resizing is less than _newlen_, these elements are lost.
 * ARGS:
 *  - newlen: new length of the 3 arrays.
 *  - hfds: pointer to the HTTP fds record.
 * RETV: returns 0 upon success, -1 upon error.
 */
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


/* httpfds_insert()
 * DESC: add new entry for file descriptor _fd_ with events mask _events_ into 
 *       HTTP file descriptor array _hfds_.
 * ARGS:
 *  - fd: file descriptor to insert.
 *  - events: events mask for file descriptor (see poll(2)).
 *  - hfds: list of HTTP file descriptors to inserts entry into.
 * RETV: 0 on success, -1 on error.
 */
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
   ++hfds->nopen;
   
   return 0;
}

/* httpfds_remove() 
 * DESC: mark entry at index _index_ from HTTP file descriptor array as removed
 *       (without changing the indices of other entries in the array).
 * ARGS:
 *  - index: index of entry to remove.
 *  - hfds: pointer to HTTP file descriptor array.
 * RETV: 0 on success, -1 on error.
 * NOTE: does not change the indices of other entries in the array. Use httpfds_pack()
 *       to compact the array after removing elements.
 */
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
      --hfds->nopen; // update number open
   }
   
   return retv;
}

/* httpfds_pack()
 * DESC: compact the HTTP file descriptor array _hfds_ by overwriting removed entries.
 * ARGS:
 *  - hfds: HTTP file descriptor array to pack.
 * RETV: the number of elements in the packed array.
 */
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
      }
      
      ++front;
   }

   hfds->count = newcount;
   
   return newcount;
}

/* httpfds_delete()
 * DESC: removes all elements in _hfds_ and frees all members of _hfds_.
 * RETV: returns 0 on success, -1 on error.
 */
int httpfds_delete(httpfds_t *hfds) {
   int retv, errsav;

   retv = 0;
   for (size_t i = 0; i < hfds->count; ++i) {
      if (httpfds_remove(i, hfds) < 0) {
         retv = -1;
         errsav = errno;
      }
   }
   
   free(hfds->fds);
   free(hfds->reqs);
   free(hfds->resps);
   memset(hfds, 0, sizeof(httpfds_t));

   errno = errsav;
   return retv;
}
