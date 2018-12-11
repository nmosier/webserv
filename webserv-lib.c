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
   
   /* obtain socket */
   if ((servsock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
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



int server_accept(int servfd) {
   socklen_t addrlen;
   struct sockaddr_in client_sa;
   int client_fd;

   /* accept socket */
   addrlen = sizeof(client_sa);
   client_fd = accept(servfd, (struct sockaddr *) &client_sa, &addrlen);

   return client_fd;
}


size_t message_bodyfree(const httpmsg_t *msg) {
   return msg->hm_body_size - (msg->hm_body_ptr - msg->hm_body);
}

// EXPLAIN DESIGN DECISION
// nonblocking -- so super-slow connections don't gum up the works
// returns -1 & error EAGAIN or EWOULDBLOCK if not available
// if *req is null, then create new. Else resume read.

void message_init(httpmsg_t *msg) {
   /* initialize struct fields */
   memset(msg, 0, sizeof(httpmsg_t));
}


int request_read(int conn_fd, httpmsg_t *req) {
   ssize_t bytes_received;
   size_t bytes_free, newsize;

   /* read until block, EOF, or \r\n */

   /* resize text buffer if necessary */
   bytes_free = message_bodyfree(req);
   if (bytes_free == 0) {
      newsize = smax(HM_BODY_INIT, req->hm_body_size * 2);
      if (message_resize_body(newsize, req) < 0) {
         perror("message_resize_body");
         return -1;
      }
      bytes_free = message_bodyfree(req);
   }
   
   /* receive bytes */
   bytes_received = recv(conn_fd, req->hm_body_ptr, bytes_free, MSG_DONTWAIT);
   if (bytes_received < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
         perror("recv");
      }
      return -1;
   }

   /* update text buffer fields */
   req->hm_body_ptr += bytes_received;
   
   /* check for terminating line */
   if (req->hm_body_ptr - req->hm_body < 4 || memcmp("\r\n\r\n", req->hm_body_ptr - 4, 4)) {
      errno = EAGAIN; // more to come
      return -1;
   }

   return 0; // success; request fully received
}

/* ERRORS:
 *  - EBADMSG: request syntax error (not a valid request)
 */
int request_parse(httpmsg_t *req) {
   char *saveptr_body;

   ////////// PARSE REQUEST LINE /////////
   char *req_line, *req_method_str, *req_version_str, *req_uri_str;
   req_line = strtok_r(req->hm_body, "\n", &saveptr_body); // has trailing '\r'

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
        (header_str = strtok_r(NULL, "\n", &saveptr_body))
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
   }
}

void request_delete(httpmsg_t *req) {
   /* delete message members */
   message_delete(req);

   /* delete request members */
   if (req) {
      free(req->hm_line.reql.uri);
      free(req->hm_line.reql.version);
   }
}

void response_delete(httpmsg_t *res) {
   message_delete(res);

   /* delete response members */
   if (res) {
      free(res->hm_line.resl.version);
   }
}

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
   size_t new_nheaders;

   /* resize headers array if full */
   if (res->hm_headers == NULL || res->hm_headers_endp >= res->hm_headers + res->hm_nheaders) {
      new_nheaders = smax(HM_NHEADERS_INIT, res->hm_nheaders * 2);
      if (message_resize_headers(new_nheaders, res) < 0) {
         perror("message_resize_headers");
         return -1;
      }
   }

   /* find & update next free header */
   hdr = res->hm_headers_endp++;
   
   /* dup strings & insert into header */
   if ((hdr->key = strdup(key)) == NULL) {
      perror("strdup");
      return -1;
   }
   if ((hdr->value = strdup(val)) == NULL) {
      perror("strdup");
      return -1;
   }
  
   return 0;
}


int response_insert_body(const char *body, size_t bodylen, const char *type, httpmsg_t *res) {
   char *bodylen_str;

   /* resize response's text */
   if (message_resize_body(bodylen, res) < 0) {
      return -1;
   }
   
   /* copy body into response */
   memcpy(res->hm_body, body, bodylen);

   /* reset the body rd/wr pointer for good measure */
   res->hm_body_ptr = res->hm_body;

   /* add Content-Type header */
   if (response_insert_header(HM_HDR_CONTENTTYPE, type, res) < 0) {
      return -1;
   }

   /* add Content-Length header */
   if (smprintf(&bodylen_str, "%zu", bodylen) < 0) {
      return -1;
   }
   if (response_insert_header(HM_HDR_CONTENTLEN, bodylen_str, res) < 0) {
      free(bodylen_str);
      return -1;
   }

   free(bodylen_str);
   return 0;
}

int response_insert_line(int code, const char *version, httpmsg_t *res) {
   httpres_stat_t *status;
   
   /* match code to response status */
   if ((status = response_find_status(code)) == NULL) {
      errno = EINVAL; // invalid code
      return -1;
   }
   res->hm_line.resl.status = status;

   /* copy version into response */
   if ((res->hm_line.resl.version = strdup(version)) == NULL) {
      return -1;
   }
   
   return 0;
}

int response_send(int conn_fd, httpmsg_t *res) {
   const char *res_fmt;
   char *hdrs_str, *hdrs_str_it, *version;
   size_t hdrs_str_len, hdrs_str_rem;
   httpmsg_header_t *hdr_it;
   const httpres_line_t *line;
   const httpres_stat_t *status;
   int send_status;
   ssize_t bytes_sent;
   
   /* format headers string */
   if ((hdrs_str = strdup("")) == NULL) {
      return -1;
   }
   hdrs_str_len = 0;
   hdrs_str_rem = 0;

   for (hdr_it = res->hm_headers, hdrs_str_it = hdrs_str;
        hdr_it != res->hm_headers_endp;
        ++hdr_it, hdrs_str_it = strchr(hdrs_str_it, '\0')) {
      
      size_t chars;

      /* calculate remaining free bytes in header string buffer */
      hdrs_str_rem = hdrs_str_len - (hdrs_str_it - hdrs_str);

      /* try to print header to buffer */
      chars = snprintf(hdrs_str_it, hdrs_str_rem + 1, "%s"HM_HDR_SEP"%s"HM_ENT_TERM,
                       hdr_it->key, hdr_it->value);

      /* if snprintf failed due to not enough space, reallocate buffer & repeat */
      while (chars > hdrs_str_rem) {
         char *hdrs_newstr;

         /* resize headers string */
         hdrs_str_len = smax(HM_HDRSTR_INIT, hdrs_str_len * 2);
         hdrs_newstr = realloc(hdrs_str, hdrs_str_len + 1); // +1 for '\0'
         if (hdrs_newstr == NULL) {
            free(hdrs_str);
            return -1;
         }

         /* recompute buffer iterator & assign new buffer */
         hdrs_str_it = hdrs_newstr + (hdrs_str_it - hdrs_str);
         hdrs_str = hdrs_newstr;
         hdrs_str_rem = hdrs_str_len - (hdrs_str_it - hdrs_str);

         /* try printing again */
         chars = snprintf(hdrs_str_it, hdrs_str_rem + 1, "%s"HM_HDR_SEP"%s"HM_ENT_TERM,
                          hdr_it->key, hdr_it->value);
      }
   }

   /* format & send message */
   res_fmt =
      HM_VERSION_PREFIX"%s %d %s"HM_ENT_TERM   // response line
      "%s"HM_ENT_TERM;                         // response headers

   line = &res->hm_line.resl;
   status = line->status;
   version = line->version;
   send_status = dprintf(conn_fd, res_fmt,
                         version, status->code, status->phrase,       // line args
                         hdrs_str);                                   // header arg
   free(hdrs_str);
   if (send_status < 0) {
      perror("dprintf");
      return -1;
   }

   

   bytes_sent = 0;
   do {
      bytes_sent += send(conn_fd, res->hm_body, res->hm_body_size, 0);
      if (bytes_sent <= 0) {
         return -1;
      }
   } while (bytes_sent < res->hm_body_size);
                         
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
   message_init(&res);
   
   /* get response code & full path */
   if ((code = document_find(docroot, &path, req)) < 0) {
      response_delete(&res);
      return -1;
   }
      
   /* insert file */
   if (code == C_OK) {
      if (response_insert_file(path, &res) < 0) {
         response_delete(&res);
         free(path);
         return -1;
      }
      free(path);
   } else {
      const char *body;
      switch (code) {
      case C_NOTFOUND:
         body = C_NOTFOUND_BODY;
         break;
      case C_FORBIDDEN:
         body = C_FORBIDDEN_BODY;
         break;
      default:
         response_delete(&res);
         errno = EBADRQC;
         return -1;
      }
      // note: strlen(body)+1 causes file to be downloaded?
      if (response_insert_body(body, strlen(body), CONTENT_TYPE_PLAIN, &res) < 0) {
         response_delete(&res);
         return -1;
      }
   }

   /* insert general headers */
   if (response_insert_genhdrs(&res) < 0) {
      response_delete(&res);
      return -1;
   }

   /* insert server headers */
   if (response_insert_servhdrs(servname, &res) < 0) {
      response_delete(&res);
      return -1;
   }
   
   /* set response line */
   if (response_insert_line(code, HM_RES_VERSION, &res) < 0) {
      response_delete(&res);
      return -1;
   }
   
   /* send request */
   if (response_send(conn_fd, &res) < 0) {
      response_delete(&res);
      return -1;
   }

   /* cleanup */
   response_delete(&res);

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


