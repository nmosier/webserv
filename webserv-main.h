#ifndef __WEBSERV_MAIN_H
#define __WEBSERV_MAIN_H

/* beloved globals */
int server_accepting;

/* macros */
#define DOCUMENT_ROOT "/home/nmosier"
#define SERVER_NAME "webserv-single/1.0"
#define PORT "1024"
#define BACKLOG 10

/* prototypes */
int server_loop(int servfd);
void handler_sigint(int signum);
void handler_sigpipe(int signum);

#endif
