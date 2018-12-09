#ifndef __WEBSERV_MSG
#define __WEBSERV_MSG

typedef struct {
   char *data;
   size_t msize; // allocated size (msize >= dsize)
   size_t dsize; // actual size of data
   char *rwp;    // read-write pointer
} msg_t;

void msg_init(msg_t *msg) {
   msg->rwp = msg->data = NULL;
   msg->msize = msg->nsize = 0;
}

ssize_t msg_read(int srcfd, msg_t *msg, const char *term) {

}

ssize_t msg_write(int dstfd, msg_t *msg) {
   
}

int msg_resize(size_t new_msize, msg_t *msg) {
   
}

void msg_delete(msg_t *msg) {
   free(msg->data);
}

#endif
