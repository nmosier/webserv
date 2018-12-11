#ifndef __WEBSERV_MULTI_H
#define __WEBSERV_MULTI_H

/* types */
struct client_thread_args {
   int client_fd;
};

/* prototypes */
void *client_loop(void *args);
int server_loop(int servfd);


#endif
