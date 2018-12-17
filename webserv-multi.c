#include <unistd.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include "webserv-lib.h"
#include "webserv-util.h"
#include "webserv-vec.h"
#include "webserv-dbg.h"
#include "webserv-contype.h"
#include "webserv-main.h"

/* macros */
#define PTHREAD_MINLEN 16

/* types */
struct client_thread_args {
   int client_fd;
   const filetype_table_t *ftypes;
};

typedef struct {
   pthread_t thd;
   struct client_thread_args *args; // malloc()ed
} client_thread_info_t;

typedef struct {
   client_thread_info_t *arr;
   size_t len;
   size_t cnt;
} client_threads_t;

/* prototypes */
void *client_loop(struct client_thread_args *thd_args);
int client_thread_info_init(client_thread_info_t *thd_info);
int client_thread_info_del(client_thread_info_t *thd_info);

/* server_loop()
 * DESC: accepts & responds to new connections by creating new threads.
 * ARGS:
 *  - servfd: server socket (already set to listening).
 *  - ftypes: pointer to content type table.
 * RETV: returns 0 on success, -1 on error.
 * NOTE: prints errors.
 */
int server_loop(int servfd, const filetype_table_t *ftypes) {
   int retv;
   client_threads_t thds;
   
   /* initialize variables */
   retv = 0;
   VECTOR_INIT(&thds);
   
   /* accept new connections & spin off new threads */
   while (retv >= 0 && server_accepting) {
      int client_fd;
      client_thread_info_t thd_info;
      
      /* accept new connection */
      if ((client_fd = server_accept(servfd)) < 0) {
         if (errno != EINTR) {
            perror("server_accept");
            retv = -1;
            break;
         } else {
            continue; // restart loop in case of interrupt
         }
      }
      
      /* initialize thread info */
      if (client_thread_info_init(&thd_info) < 0) {
         perror("client_thread_info_init");
         if (close(client_fd) < 0) {
            perror("close");
         }
         retv = -1;
         break;
      }
      thd_info.args->client_fd = client_fd;
      thd_info.args->ftypes = ftypes;
      
      /* spin off new thread */
      if (pthread_create(&thd_info.thd, NULL, (void *(*)(void *)) client_loop, thd_info.args)) {
         perror("pthread_create");
         if (close(client_fd) < 0) {
            perror("close");
         }
         retv = -1;
         break;
      }

      /* add thread info to list */
      if (VECTOR_INSERT(&thd_info, &thds) < 0) {
         perror("pthreads_insert");
         if (close(client_fd) < 0) {
            perror("close");
         }
         client_thread_info_del(&thd_info);
         retv = -1;
         break;
      }
      
   }

   /* cleanup */
   
   /* shutdown server socket (reading) */
   if (shutdown(servfd, SHUT_RD) < 0) {
      perror("shutdown");
   }

   /* wait for threads to die */
   printf("waiting for %zu connections to close...\n", thds.cnt);
   for (size_t i = 0; i < thds.cnt; ++i) {
      void *thd_retv;

      /* join thread */
      if (pthread_join(thds.arr[i].thd, &thd_retv) < 0) {
         retv = -1;
      }
      if (thd_retv == (void *) -1) {
         /* error occurred in thread */
         retv = -1;
      }
   }
   VECTOR_DELETE(&thds, client_thread_info_del);

   return retv;
}


/* client_loop()
 * DESC: reads request from client socket, sends response, closes client socket, and dies.
 * ARGS:
 *  - thd_args: pointer to client socket thread's arguments, which contains a pointer
 *              to a content type table and the client socket's file descriptor.
 * RETV: returns (void *) 0 upon success, (void *) -1 upon error.
 */
void *client_loop(struct client_thread_args *thd_args) {
   int client_fd;
   httpmsg_t req, res;
   const filetype_table_t *ftypes;
   int msg_stat, msg_err;
   void *retv;

   /* initialize variables */
   client_fd = thd_args->client_fd;
   ftypes = thd_args->ftypes;
   retv = (void *) 0;
   request_init(&req);
   response_init(&res);

   /* read request to completion */
   while ((msg_stat = request_read(client_fd, &req)) < 0
          && (msg_err = message_error(errno)) == MSG_EAGAIN) {}

   /* check for any read errors */
   if (msg_stat < 0) {
      if (msg_err == MSG_ECONN) {
         printf("connection to client socket %d interrupted while receiving\n", client_fd);
      } else {
         perror("request_read");
         retv = (void *) -1;
      }
      goto cleanup;
   }

   /* parse request */
   if (request_parse(&req) < 0) {
      perror("request_parse");
      retv = (void *) -1;
      goto cleanup;
   }
   
   /* create response */
   if (server_handle_req(client_fd, DOCUMENT_ROOT, SERVER_NAME, &req, &res, ftypes) < 0) {
      perror("server_handle_req");
      retv = (void *) -1;
      goto cleanup;
   }

   /* send response */
   while ((msg_stat = response_send(client_fd, &res)) < 0
          && (msg_err = message_error(errno)) == MSG_EAGAIN) {}
   if (msg_stat < 0) {
      if (msg_err == MSG_ECONN) {
         printf("connection to client socket %d interrupted while sending\n", client_fd);
      } else {
         perror("response_send");
         retv = (void *) -1;
      }
      goto cleanup;
   }

 cleanup:
   if (close(client_fd) < 0) {
      perror("close");
      retv = (void *) -1;
   }
   request_delete(&req);
   response_delete(&res);

   return retv;
}

/* client_thread_info_init()
 * DESC: initializes a client thread info record.
 * RETV: 0 upon success, -1 upon error.
 */
int client_thread_info_init(client_thread_info_t *thd_info) {
   if ((thd_info->args = malloc(sizeof(*thd_info->args))) == NULL) {
      return -1;
   }

   return 0;
}

/* client_thread_info_del()
 * DESC: deletes a client thread info record.
 * RETV: 0.
 */
int client_thread_info_del(client_thread_info_t *thd_info) {
   if (thd_info) {
      free(thd_info->args);
   }

   return 0;
}
