#ifndef __WEBSERV_SINGLE_H
#define __WEBSERV_SINGLE_H

typedef struct {
   struct pollfd *fds;
   httpmsg_t *reqs;
   size_t len; // length of allocated array
   size_t count; // number of fds currently in array
} httpfds_t;


int server_accept(int servfd, httpfds_t *hfds);
int server_loop(int servfd);

void httpfds_init(httpfds_t *hfds);
int httpfds_resize(size_t newlen, httpfds_t *hfds);
int httpfds_insert(int fd, int events, httpfds_t *hfds);
int httpfds_remove(size_t index, httpfds_t *hfds);
size_t httpfds_pack(httpfds_t *hfds);
int httpfds_cleanup(httpfds_t *hfds);

int server_loop();



#endif
