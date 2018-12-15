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
#include "webserv-main.h"

/* macros */
#define PTHREAD_MINLEN 16

/* types */
struct client_thread_args {
   int client_fd;
};

typedef struct {
   pthread_t *arr;
   size_t len;
   size_t cnt;
} pthreads_t;

/* prototypes */
void *client_loop(void *args);
void pthreads_init(pthreads_t *thds);
int pthreads_resize(size_t newlen, pthreads_t *thds);
size_t pthreads_rem(pthreads_t *thds);
int pthreads_insert(pthread_t thd, pthreads_t *thds);
int pthreads_remove(pthread_t thd, pthreads_t *thds);
void pthreads_delete(pthreads_t *thds);

int server_loop(int servfd) {
   int retv;
   pthreads_t thds;

   /* initialize variables */
   retv = 0;
   VECTOR_INIT(&thds);

   /* accept new connections & spin off new threads */
   while (retv >= 0 && server_accepting) {
      int client_fd;
      pthread_t thd;
      struct client_thread_args thd_args;

      /* accept new connection */
      if ((client_fd = server_accept(servfd)) < 0) {
         if (errno != EINTR) {
            perror("server_accept");
            retv = -1;
            break;
         }
         continue; // restart loop in case of interrupt
      }
      
      /* spin off new thread */
      thd_args.client_fd = client_fd;
      if (pthread_create(&thd, NULL, client_loop, &thd_args)) {
         /* don't exit -- wait for other threads to die */
         perror("pthread_create");
         retv = -1;
         break;
      }

      /* add thread to list */
      if (VECTOR_INSERT(&thd, &thds) < 0) {
         perror("pthreads_insert");
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
      if (pthread_join(thds.arr[i], &thd_retv) < 0) {
         retv = -1;
      }
      if (thd_retv == (void *) -1) {
         /* error occurred in thread */
         retv = -1;
      }
   }
   VECTOR_DELETE(&thds, NULL);

   return retv;
}


void *client_loop(void *args) {
   struct client_thread_args *thd_args;
   int client_fd;
   httpmsg_t req, res;
   int msg_stat, msg_err;
   void *retv;

   /* initialize variables */
   thd_args = (struct client_thread_args *) args;
   client_fd = thd_args->client_fd;
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
   if (server_handle_req(client_fd, DOCUMENT_ROOT, SERVER_NAME, &req, &res) < 0) {
      perror("server_handle_get");
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

/*
void pthreads_init(pthreads_t *thds) {
   vector_init(thds, sizeof(*thds));
}

int pthreads_resize(size_t newlen, pthreads_t *thds) {
   return vector_resize(newlen, (void **) &thds->arr, &thds->cnt, &thds->len,
                        sizeof(*thds->arr));
}

size_t pthreads_rem(pthreads_t *thds) {
   return vector_rem(thds->cnt, thds->len);
}

int pthreads_insert(pthread_t thd, pthreads_t *thds) {
   return vector_insert(&thd, (void **) &thds->arr, &thds->cnt, &thds->len, sizeof(*thds->arr));
}

int pthreads_compare(void *thd1, void *thd2) {
   return *((pthread_t *) thd1) - *((pthread_t *) thd2);
}

int pthreads_remove(pthread_t thd, pthreads_t *thds) {
   return vector_remove(&thd, (void **) &thds->arr, &thds->cnt, sizeof(*thds->arr),
                        pthreads_compare);
}

void pthreads_delete(pthreads_t *thds) {
   vector_delete(thds, (void **) &thds->arr, sizeof(*thds));
}
*/
