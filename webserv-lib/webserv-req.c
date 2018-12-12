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
#include "webserv-lib.h"
#include "webserv-util.h"
#include "webserv-dbg.h"

/*************** HTTP REQUEST FUNCTIONS ***************/
void request_init(httpmsg_t *req) {
   message_init(req);
}

int request_read(int conn_fd, httpmsg_t *req) {
   ssize_t bytes_received;
   size_t bytes_free, newsize;

   /* read until block, EOF, or \r\n */

   /* resize text buffer if necessary */
   bytes_free = message_textfree(req);
   if (bytes_free == 0) {
      newsize = smax(HM_TEXT_INIT, req->hm_text_size * 2);
      if (message_resize_text(newsize, req) < 0) {
         perror("message_resize_text");
         return -1;
      }
      bytes_free = message_textfree(req);
   }
   
   /* receive bytes */
   bytes_received = recv(conn_fd, req->hm_text_ptr, bytes_free, MSG_DONTWAIT);
   if (bytes_received < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
         perror("recv");
      }
      return -1;
   }

   /* update text buffer fields */
   req->hm_text_ptr += bytes_received;
   
   /* check for terminating line */
   if (req->hm_text_ptr - req->hm_text < 4 || memcmp("\r\n\r\n", req->hm_text_ptr - 4, 4)) {
      errno = EAGAIN; // more to come
      return -1;
   }

   return 0; // success; request fully received
}

/* ERRORS:
 *  - EBADMSG: request syntax error (not a valid request)
 */
int request_parse(httpmsg_t *req) {
   char *saveptr_text;
   ////////// PARSE REQUEST LINE /////////
   char *req_line, *req_method_str, *req_version_str, *req_uri_str;
   req_line = strtok_r(req->hm_text, "\n", &saveptr_text); // has trailing '\r'

   /* parse request line method */
   if ((req_method_str = strtok(req_line, " "))) {
      req->hm_line.reql.method = hr_str2meth(req_method_str);
   }
   if (req_method_str == NULL || req->hm_line.reql.method < 0) {
      errno = EBADMSG;
      return -1;
   }

   /* parse request line URI */
   req_uri_str = strtok(NULL, " ");
   if (req_uri_str == NULL) {
      errno = EBADMSG;
      return -1;
   }
   if ((req->hm_line.reql.uri = strdup(req_uri_str)) == NULL) {
      return -1;
   }
      

   /* parse request line HTTP version */
   req_version_str = strtok(NULL, "\r"); // last item in line
   req_version_str = strskip(HM_VERSION_PREFIX, req_version_str);
   if (req_version_str == NULL) {
      errno = EBADMSG;
      return -1;
   }
   if ((req->hm_line.reql.version = strdup(req_version_str)) == NULL) {
      return -1;
   }
   
   ///////// PARSE REQUEST HEADERS /////////
   httpmsg_header_t *header_it;
   char *header_str, *val_str, *key_str;
   for (header_it = req->hm_headers;
        (header_str = strtok_r(NULL, "\n", &saveptr_text))
           && strcmp(header_str, "\r");
        ++header_it) {

      /* check if array full */
      if (header_it == req->hm_headers + req->hm_nheaders) {
         size_t header_i = header_it - req->hm_headers; // current index
         size_t new_nheaders = smax(HM_NHEADERS_INIT, req->hm_nheaders * 2);

         /* expand header size */
         if (message_resize_headers(new_nheaders, req) < 0) {
            return -1;
         }

         header_it = req->hm_headers + header_i; // update header iterator         
      }
      
      /* parse single request header */

      /* get key */
      if ((key_str = strtok(header_str, ":")) == NULL) {
         errno = EBADMSG;
         return -1;
      }
      /* get value */
      if ((val_str = strtok(NULL, "\r")) == NULL) {
         errno = EBADMSG;
         return -1;
      } else {
         /* strip leading whitespace */
         val_str = strstrip(val_str, " ");
      }
      /* set key & value */
      if ((header_it->key = strdup(key_str)) == NULL) {
         return -1;
      }
      if ((header_it->value = strdup(val_str)) == NULL) {
         return -1;
      }
   }

   req->hm_headers_endp = header_it;
   
   return 0;
}

void request_delete(httpmsg_t *req) {
   /* delete message members */
   message_delete(req);

   /* delete request members */
   if (req) {
      free(req->hm_line.reql.uri);
      free(req->hm_line.reql.version);
   }

   /* zero out record */
   memset(req, 0, sizeof(httpmsg_t));
}

// finds document
// returns response code & completes path
// NOTE: pathp ONLY malloc()ed if retval is C_OK
int request_document_find(const char *docroot, char **pathp, httpmsg_t *req) {
   const char *rsrc;
   struct stat rsrc_stat;
   int st_mode;

   /* locate resource in request line */
   rsrc = req->hm_line.reql.uri;

   /* get entire path */
   if (smprintf(pathp, "%s%s", docroot, rsrc) < 0) {
      return -1;
   }

   /* stat resource */
   if (stat(*pathp, &rsrc_stat) < 0) {
      free(*pathp);
      switch (errno) {
      case EACCES:
         return C_FORBIDDEN;
      case ENOENT:
      case ENOTDIR:
         return C_NOTFOUND;
      default:
         return -1;
      }
   }

   st_mode = rsrc_stat.st_mode;

   /* check if resource exists & have read permissions */
   if (!(S_ISREG(st_mode) && (st_mode | S_IROTH))) {
      free(*pathp);
      return C_FORBIDDEN;
   }

   /* only here does pathp remain allocated */
   return C_OK;
}
