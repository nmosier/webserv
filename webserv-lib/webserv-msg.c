#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>
#include <fcntl.h>
#include <sys/utsname.h>
#include "webserv-util.h"
#include "webserv-dbg.h"
#include "webserv-msg.h"

/* message_textfree() 
 * DESC: returns number of unused bytes in the text buffer of _msg_.
 */
size_t message_textfree(const httpmsg_t *msg) {
   return msg->hm_text_size - (msg->hm_text_ptr - msg->hm_text);
}



/* message_init()
 * DESC: initialize HTTP message.
 * NOTE: you should probably be calling request_init() or response_init().
 */
void message_init(httpmsg_t *msg) {
   /* initialize struct fields */
   memset(msg, 0, sizeof(httpmsg_t));
}


/* message_delete()
 * DESC: deletes _msg_'s members shared by both requests and responses.
 * NOTE: calls to message_init() and ONLY calls to message_init() should
 *       be matched with a call to message_delete(). Otherwise, use 
 *       request_delete() or response_init().
 */
void message_delete(httpmsg_t *msg) {
   if (msg) {
      /* free header members */
      if (msg->hm_headers) {
         for (httpmsg_header_t *hdr_it = msg->hm_headers;
              hdr_it < msg->hm_headers_endp; ++hdr_it) {
            free(hdr_it->key);
            free(hdr_it->value);
         }
      }
      
      /* free headers array */
      free(msg->hm_headers);

      /* free body */
      free(msg->hm_body);

      /* free text */
      free(msg->hm_text);
   }
}


/* message_resize_headers()
 * DESC: resizes the number of headers in HTTP message _msg_'s header array,
 *       truncating off exisiting elements if necessary.
 * ARGS:
 *  - new_nheaders: new length of header array.
 *  - msg: pointer to HTTP message (request or response).
 * RETV: returns 0 on success, -1 on error.
 */
int message_resize_headers(size_t new_nheaders, httpmsg_t *msg) {
   httpmsg_header_t *newheaders;
   size_t endp_index;

   /* calculate index of end pointer */
   endp_index = msg->hm_headers_endp - msg->hm_headers;
   
   /* reallocate headers array */
   newheaders = realloc(msg->hm_headers, (new_nheaders+1) * sizeof(httpmsg_header_t));
   if (newheaders == NULL) {
      return -1;
   }
   msg->hm_headers = newheaders;
   
   /* zero out uninitialized memory */
   if (new_nheaders > msg->hm_nheaders) {
      memset(newheaders + msg->hm_nheaders + 1, 0,
             sizeof(httpmsg_header_t) * (new_nheaders - msg->hm_nheaders));
   } else {
      /* zero out last element */
      memset(newheaders + new_nheaders, 0, sizeof(httpmsg_header_t));
   }
   msg->hm_nheaders = new_nheaders;
   msg->hm_headers_endp = newheaders + endp_index;
   
   return 0;
}

/* message_resize_body()
 * DESC: resize the body of _msg_.
 * ARGS:
 *  - newsize: new size of body.
 *  - msg: HTTP message (req. or res.) whose body should be resized.
 * RETV: 0 on success, -1 on error.
 */
int message_resize_body(size_t newsize, httpmsg_t *msg) {
   char *newbody;

   /* reallocate text buffer */
   newbody = realloc(msg->hm_body, newsize);
   if (newbody == NULL) {
      return -1;
   }
   msg->hm_body_size = newsize;
   msg->hm_body_ptr += newbody - msg->hm_body;
   msg->hm_body = newbody;

   return 0;
}

/* see message_resize_body()'s documentation, sed 's/body/text/g' */
int message_resize_text(size_t newsize, httpmsg_t *msg) {
   char *newtext;

   /* reallocate text buffer */
   newtext = realloc(msg->hm_text, newsize);
   if (newtext == NULL) {
      return -1;
   }
   msg->hm_text_size = newsize;
   msg->hm_text_ptr += newtext - msg->hm_text;
   msg->hm_text = newtext;
   
   return 0;
}



/* message_error()
 * DESC: classifes error of [ message_* | request_* | response_* ] function
 * ARGS:
 *  - errno: error number upon request_read()'s exit
 * RETV:
 *  - MSG_ESUCCESS if no error occurred
 *  - MSG_EAGAIN if the operation simply needs to be tried again
 *  - MSG_ECONN if the socket/pipe connection was broken
 *  - MSG_ESERV if an internal error occurred (indicates bug)
 */
int message_error(int msg_errno) {
   switch (msg_errno) {
   case 0:
      return MSG_ESUCCESS;
      
   case EAGAIN:
#if EAGAIN != EWOULDBLOCK
   case EWOULDBLOCK:
#endif
   case EINTR:
   case ECONNRESET:
      return MSG_EAGAIN;

   case EPIPE:
   case ECONNABORTED:
   case ECONNREFUSED:
      return MSG_ECONN;

   default:
      return MSG_ESERV;
   }
}
