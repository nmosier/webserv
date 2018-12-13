#ifndef __WEBSERV_FDS_H
#define __WEBSERV_FDS_H

/* types */
typedef struct {
   struct pollfd *fds;
   httpmsg_t   *reqs;
   httpmsg_t  *resps;
   size_t        len; // length of allocated array
   size_t      count; // number of fds currently in array
   size_t      nopen; // number of fds that are open (count == nopen after httpfds_pack())
} httpfds_t;

/* prototypes */
void   httpfds_init(httpfds_t *hfds);
int    httpfds_resize(size_t newlen, httpfds_t *hfds);
int    httpfds_insert(int fd, int events, httpfds_t *hfds);
int    httpfds_remove(size_t index, httpfds_t *hfds);
size_t httpfds_pack(httpfds_t *hfds);
int    httpfds_delete(httpfds_t *hfds);

/* defines */
#define HTTPFDS_MINLEN 16

#endif
