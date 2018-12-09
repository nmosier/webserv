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

#define DEBUG 1

// prints errors
int server_start(const char *port, int backlog) {
   int servsock_fd;
   struct addrinfo *res;
   int gai_stat;
   int error;

   /* initialize variables */
   servsock_fd = -1;
   res = NULL;
   error = 0; // error=1 if error occurred
   
   /* obtain (nonblocking) socket */
   if ((servsock_fd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0)) < 0) {
      perror("socket");
      error = 1;
      goto cleanup;
   }

   /* get address info */
   struct addrinfo hints = {0};
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_flags = AI_PASSIVE;
   if ((gai_stat = getaddrinfo(NULL, port, &hints, &res))) {
      /* getaddrinfo() error */
      fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gai_stat));
      error = 1;
      goto cleanup;
   }
   
   /* bind socket to port */
   if (bind(servsock_fd, res->ai_addr, res->ai_addrlen) < 0) {
      perror("bind");
      error = 1;
      goto cleanup;
   }

   /* listen for connections */
   if (listen(servsock_fd, backlog) < 0) {
      perror("listen");
      error = 1;
      goto cleanup;
   }

 cleanup:
   /* close server socket (if error occurred) */
   if (error && servsock_fd >= 0 && close(servsock_fd) < 0) {
      perror("close");
   }

   /* free res addrinfo (unconditionally) */
   if (res) {
      freeaddrinfo(res);
   }

   /* return -1 on error, server socket on success */
   return error ? -1 : servsock_fd;
}

// EXPLAIN DESIGN DECISION
// nonblocking -- so super-slow connections don't gum up the works
// returns -1 & error EAGAIN or EWOULDBLOCK if not available
// if *req is null, then create new. Else resume read.

int message_init(httpmsg_t *msg) {
   /* initialize struct fields */
   memset(msg, 0, sizeof(httpmsg_t));
   msg->hm_body = -1;

   /* allocate & initialize buffer */
   msg->hm_text_endp = msg->hm_text = malloc(HTTPMSG_TEXTSZ + 1); // +1 for null terminator
   if (msg->hm_text == NULL) {
      message_delete(msg);
      return -1;
   }
   msg->hm_text_size = HTTPMSG_TEXTSZ;
   
   /* allocate & initialize headers */
   msg->hm_headers = msg->hm_headers_endp = calloc(HTTPMSG_NHEADS,
                                                   sizeof(httpmsg_header_t));
   if (msg->hm_headers == NULL) {
      message_delete(msg);
      return -1;
   }
   msg->hm_nheaders = HTTPMSG_NHEADS;

   return 0;
}


int request_read(int servsock_fd, int conn_fd, httpmsg_t *req) {
   int msg_done;
   ssize_t bytes_received;
   size_t bytes_free;

   /* read until block, EOF, or \r\n */
   msg_done = 0; // whether terminating \r\n has been encountered
   do {
      /* resize text buffer if necessary */
      bytes_free = HM_TEXTFREE(req);
      if (bytes_free == 0) {
         if (message_resize_text(req->hm_text_size * 2, req) < 0) {
            perror("request_resize_text");
            return REQ_RD_RERROR;
         }
         bytes_free = HM_TEXTFREE(req); // update free byte count
      }

      /* receive bytes */
      bytes_received = recv(conn_fd, req->hm_text_endp, bytes_free, MSG_DONTWAIT);

      /* if bytes received, update request fields */
      if (bytes_received > 0) {
         /* update text buffer fields */
         req->hm_text_endp += bytes_received;
         *(req->hm_text_endp) = '\0';
         
         /* check for terminating line */
         if (req->hm_text_endp - req->hm_text >= 4 &&
             strcmp("\r\n\r\n", req->hm_text_endp - 4) == 0) {
            msg_done = 1;
         }
      }
   } while (bytes_received > 0 && !msg_done);

   /* check for errors */
   if (bytes_received < 0) {
      /* check if due to blocking */
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
         return REQ_RD_RAGAIN;
      }
      /* otherwise, report error */
      perror("recv");
      return REQ_RD_RERROR;
   }
   
   return REQ_RD_RSUCCESS;
}





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
      return REQ_PRS_RSYNTAX;
   }

   /* parse request line URI */
   req_uri_str = strtok(NULL, " ");
   if (req_uri_str == NULL) {
      return REQ_PRS_RSYNTAX;
   }
   req->hm_line.reql.uri = HM_STR2OFF(req_uri_str, req);

   /* parse request line HTTP version */
   req_version_str = strtok(NULL, "\r"); // last item in line
   req_version_str = strskip(HM_VERSION_PREFIX, req_version_str);
   if (req_version_str == NULL) {
      return REQ_PRS_RSYNTAX;
   }
   req->hm_line.reql.version = HM_STR2OFF(req_version_str, req);
   
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
         size_t new_nheaders = req->hm_nheaders * 2;

         /* expand header size */
         if (message_resize_headers(new_nheaders, req) < 0) {
            return REQ_PRS_RERROR;
         }

         header_it = req->hm_headers + header_i; // update header iterator         
      }
      
      /* parse single request header */

      /* get key */
      if ((key_str = strtok(header_str, ":")) == NULL) {
         return REQ_PRS_RSYNTAX;
      }
      /* get value */
      if ((val_str = strtok(NULL, "\r")) == NULL) {
         return REQ_PRS_RSYNTAX;
      } else {
         /* strip leading whitespace */
         val_str = strstrip(val_str, " ");
      }
      /* set key & value */
      header_it->key = HM_STR2OFF(key_str, req);
      header_it->value = HM_STR2OFF(val_str, req);
   }
   
   return REQ_PRS_RSUCCESS;
}

void message_delete(httpmsg_t *msg) {
   if (msg) {
      /* free members */
      free(msg->hm_headers);
      free(msg->hm_text);
   }
}

int message_resize_headers(size_t new_nheaders, httpmsg_t *msg) {
   httpmsg_header_t *newheaders;

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

   return 0;
}

int message_resize_text(size_t newsize, httpmsg_t *msg) {
   char *newtext;

   /* reallocate text buffer */
   newtext = realloc(msg->hm_text, newsize + 1);
   if (newtext == NULL) {
      return -1;
   }
   msg->hm_text_size = newsize;
   msg->hm_text_endp += newtext - msg->hm_text;
   msg->hm_text = newtext;

   return 0;
}


static httpres_stat_t hr_stats[] = {
   {C_OK, "OK"},
   {C_NOTFOUND, "Not found"},
   {C_FORBIDDEN, "Forbidden"},
   {0, 0}
};

httpres_stat_t *response_find_status(int code) {
   httpres_stat_t *stat_it;

   /* find response status with matching code
    * (note: stat_it->phrase will be NULL at end of list)
    */
   for (stat_it = hr_stats; stat_it->phrase && stat_it->code != code; ++stat_it) {}
   return stat_it->phrase ? stat_it : NULL;
}


/// response stuff
int response_insert_header(const char *key, const char *val, httpmsg_t *res) {
   httpmsg_header_t *hdr;
   size_t key_len, val_len;
   char *text_ptr;

   /* resize if full */
   if (res->hm_headers_endp == res->hm_headers + res->hm_nheaders) {
      if (message_resize_headers(res->hm_nheaders * 2, res) < 0) {
         perror("message_resize_headers");
         return -1;
      }
   }
   
   hdr = res->hm_headers_endp;
   
   /* find length of header's underlying strings */
   key_len = strlen(key) + 1;
   val_len = strlen(val) + 1;

   /* make sure there's enough space to copy strings into response */
   if (HM_TEXTFREE(res) < key_len + val_len) {
      if (message_resize_text(res->hm_text_size * 2, res) < 0) {
         perror("message_resize_text");
         return -1;
      }
   }
   text_ptr = res->hm_text_endp + 1; // +1 so previous strings are terminated by '\0'

   /* copy header key string & pointer */
   memcpy(text_ptr, key, key_len); // +1 for null terminator
   hdr->key = HM_STR2OFF(text_ptr, res);

   text_ptr += key_len;
   
   /* copy header into array */
   memcpy(text_ptr, val, val_len);
   hdr->value = HM_STR2OFF(text_ptr, res);

   res->hm_text_endp += key_len + val_len;
   ++res->hm_headers_endp;
   
   return 0;
}

// now inserts content-type header too
int response_insert_body(const char *body, size_t bodylen, const char *type, httpmsg_t *res) {
   char bodylen_str[INTLEN(size_t) + 1];
   
   /* resize response's text if necessary */
   if (HM_TEXTFREE(res) < bodylen) {
      if (message_resize_text(res->hm_text_size + (bodylen + 1) - HM_TEXTFREE(res), res) < 0) {
         return -1;
      }
   }
   
   /* copy body into response's text */
   memcpy(res->hm_text_endp + 1, body, bodylen + 1); // +1 for null term.

   /* update response pointers */
   res->hm_body = HM_STR2OFF(res->hm_text_endp + 1, res);
   res->hm_text_endp += bodylen + 1;

   /* add Content-Type header */
   if (response_insert_header(HM_HDR_CONTENTTYPE, type, res) < 0) {
      return -1;
   }


   /* add Content-Length header */
   if (sprintf(bodylen_str, "%zu", bodylen) < 0) {
      return -1;
   }
   if (response_insert_header(HM_HDR_CONTENTLEN, bodylen_str, res) < 0) {
      return -1;
   }
   
   return 0;
}

int response_insert_line(int code, const char *version, httpmsg_t *res) {
   httpres_stat_t *status;
   size_t version_len;
   char *version_str;
   
   /* match code to response status */
   if ((status = response_find_status(code)) == NULL) {
      errno = EINVAL; // invalid code
      return -1;
   }
   res->hm_line.resl.status = status;

   /* copy version into response text */
   version_len = strlen(version) + 1;
   if (HM_TEXTFREE(res) < version_len) {
      if (message_resize_text(res->hm_text_size * 2, res) < 0) {
         return -1;
      }
   }
   version_str = strcpy(res->hm_text_endp + 1, version);
   res->hm_text_endp += version_len;
   res->hm_line.resl.version = HM_STR2OFF(version_str, res);
   
   return 0;
}

// generate & send response
// use dprintf!
// know that text size is enough to fit all headers

int response_send(int conn_fd, httpmsg_t *res) {
   const char *res_fmt;
   char *hdrs_str, *hdrs_str_it, *hdr_key, *hdr_val, *body, *version;
   size_t hdrs_str_len;
   httpmsg_header_t *hdr_it;
   httpres_line_t *line;
   httpres_stat_t *status;
   int send_status;

   /* ensure message has a body, even if empty string */
   if (res->hm_body < 0) {
      res->hm_body = HM_STR2OFF(res->hm_text_endp, res);
   }

   /* format headers string */
   hdrs_str_len = res->hm_text_size + (HM_HDR_SEPLEN+HM_ENT_TERMLEN)*res->hm_nheaders;
   if ((hdrs_str = malloc(hdrs_str_len + 1)) == NULL) {
      return -1;
   }
   
   for (hdr_it = res->hm_headers, hdrs_str_it = hdrs_str;
        hdr_it != res->hm_headers_endp;
        ++hdr_it, hdrs_str_it = strchr(hdrs_str_it, '\0')) {
      hdr_key = HM_OFF2STR(hdr_it->key, res);
      hdr_val = HM_OFF2STR(hdr_it->value, res);
      if (sprintf(hdrs_str_it, "%s"HM_HDR_SEP"%s"HM_ENT_TERM, hdr_key, hdr_val) < 0) {
         free(hdrs_str);
         return -1;
      }
   }

   /* format & send message header */
   res_fmt =
      HM_VERSION_PREFIX"%s %d %s"HM_ENT_TERM   // response line
      "%s"HM_ENT_TERM                          // response headers
      "%s";                                    // response body
   line = &res->hm_line.resl;
   status = line->status;
   version = HM_OFF2STR(line->version, res);
   body = HM_OFF2STR(res->hm_body, res);
   send_status = dprintf(conn_fd, res_fmt,
                         version, status->code, status->phrase,       // line args
                         hdrs_str,                                    // header arg
                         body);                                       // body arg

   if (DEBUG) {
      send_status = printf(res_fmt,
                           version, status->code, status->phrase,       // line args
                           hdrs_str,                                    // header arg
                           body);                                       // body arg
      
   }


   free(hdrs_str);
   if (send_status < 0) {
      perror("dprintf");
      return -1;
   }
   
   return 0;
}

// higher level operation than response_insert_body() 
int response_insert_file(const char *path, httpmsg_t *res) {
   int fd;
   struct stat fd_info;
   off_t fd_size;
   char *last_mod;
   char *body;
   char content_type[CONTENT_TYPE_MAXLEN];
   int retv;

   /* initialize variables (checked at cleanup) */
   fd = -1;
   last_mod = NULL;
   body = MAP_FAILED;
   retv = -1; // error by default
   
   /* open file */
   if ((fd = open(path, O_RDONLY)) < 0) {
      goto cleanup;
   }
   
   /* get file info */
   if (fstat(fd, &fd_info) < 0) {
      goto cleanup;
   }
   fd_size = fd_info.st_size;

   /* map file into memory */
   if ((body = mmap(NULL, fd_size, PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
      goto cleanup;
   }

   /* insert into response as body */
   get_content_type(path, content_type);
   if (response_insert_body(body, fd_size, content_type, res) < 0) {
      goto cleanup;
   }
   
   /* insert Last-Modified header */
   if (hm_fmtdate(&fd_info.st_mtim.tv_sec, &last_mod) < 0) {
      goto cleanup;
   }
   if (response_insert_header(HM_HDR_LASTMODIFIED, last_mod, res) < 0) {
      goto cleanup;
   }

   retv = 0; // success (so far)
   
 cleanup:
   /* cleanup */
   if (body != MAP_FAILED) {
      if (munmap(body, fd_size) < 0) {
         retv = -1;
      }
   }
   if (fd >= 0) {
      if (close(fd) < 0) {
         retv = -1;
      }
   }
   if (last_mod) {
      free(last_mod);
   }
   
   return retv;
}

// genhdrs = general headers
int response_insert_genhdrs(httpmsg_t *res) {
   time_t curtime;
   char *date;

   /* Date */
   curtime = time(NULL);
   if (hm_fmtdate(&curtime, &date) < 0) {
      return -1;
   }
   if (response_insert_header(HM_HDR_DATE, date, res) < 0) {
      free(date);
      return -1;
   }

   /* cleanup */
   free(date);
   
   return 0;
}

// TODO
int response_insert_servhdrs(const char *servname, httpmsg_t *res) {
   struct utsname sysinfo;
   char *serv;
   
   /* Server */
   if (uname(&sysinfo) < 0) {
      return -1;
   }
   if (smprintf(&serv, "%s/%s %s", sysinfo.sysname, sysinfo.release, servname) < 0) {
      return -1;
   }
   if (response_insert_header(HM_HDR_SERVER, serv, res) < 0) {
      free(serv);
      return -1;
   }
   free(serv);

   /* Connection */
   if (response_insert_header(HM_HDR_CONNECTION, "close", res) < 0) {
      return -1;
   }
   
   return 0;
}

int server_handle_req(int conn_fd, const char *docroot, const char *servname, httpmsg_t *req) {
   switch (req->hm_line.reql.method) {
   case M_GET:
      return server_handle_get(conn_fd, docroot, servname, req);
   default:
      errno = EBADRQC;
      return -1;
   }
}

// handle GET request
// TODO: make case statement table-driven, not switch case
int server_handle_get(int conn_fd, const char *docroot, const char *servname, httpmsg_t *req) {
   httpmsg_t res;
   char *path;
   int code;

   /* create response */
   if (message_init(&res) < 0) {
      return -1;
   }
   
   /* get response code & full path */
   if ((code = document_find(docroot, &path, req)) < 0) {
      message_delete(&res);
      return -1;
   }
      
   /* insert file */
   if (code == C_OK) {
      if (response_insert_file(path, &res) < 0) {
         message_delete(&res);
         free(path);
         return -1;
      }
      free(path);
   } else {
      const char *body;
      free(path);
      switch (code) {
      case C_NOTFOUND:
         body = C_NOTFOUND_BODY;
         break;
      case C_FORBIDDEN:
         body = C_FORBIDDEN_BODY;
         break;
      default:
         message_delete(&res);
         errno = EBADRQC;
         return -1;
      }
      if (response_insert_body(body, strlen(body)+1, CONTENT_TYPE_PLAIN, &res) < 0) {
         message_delete(&res);
         return -1;
      }
   }

   /* insert general headers */
   if (response_insert_genhdrs(&res) < 0) {
      message_delete(&res);
      return -1;
   }

   /* insert server headers */
   if (response_insert_servhdrs(servname, &res) < 0) {
      message_delete(&res);
      return -1;
   }
   
   /* set response line */
   if (response_insert_line(code, HM_RES_VERSION, &res) < 0) {
      message_delete(&res);
      return -1;
   }
   
   /* send request */
   if (response_send(conn_fd, &res) < 0) {
      message_delete(&res);
      return -1;
   }

   /* cleanup */
   message_delete(&res);
   //free(path);
   return 0;
}


// finds document
// returns response code & completes path
// NOTE: pathp ONLY malloc()ed if retval is C_OK
int document_find(const char *docroot, char **pathp, httpmsg_t *req) {
   const char *rsrc;
   struct stat rsrc_stat;
   int st_mode;

   /* locate resource in request line */
   rsrc = HM_OFF2STR(req->hm_line.reql.uri, req);

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






typedef struct {
   const char *str;
   httpreq_method_t meth;
} hr_str2meth_t;

static hr_str2meth_t hr_str2meth_v[] = {
   {"GET", M_GET},
   {0,            0}
};


httpreq_method_t hr_str2meth(const char *str) {
   for (hr_str2meth_t *it = hr_str2meth_v; it->str; ++it) {
      if (strcmp(it->str, str) == 0) {
         return it->meth;
      }
   }
   return -1;
}

const char * hr_meth2str(httpreq_method_t meth) {
   for (hr_str2meth_t *it = hr_str2meth_v; it->str; ++it) {
      if (meth == it->meth) {
         return it->str;
      }
   }
   return NULL;
}


