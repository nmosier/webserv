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

/* response_init(): initialize response. */
void response_init(httpmsg_t *res) {
   message_init(res);
}

/* response_delete(): delete response. */
void response_delete(httpmsg_t *res) {
   message_delete(res);

   /* delete response members */
   if (res) {
      free(res->hm_line.resl.version);
   }
}

/* response_send()
 * DESC: send response (NONBLOCKING/ASYNCHRONOUS).
 * ARGS:
 *  - conn_fd: client socket to send response over.
 *  - res: response to send.
 * RETV: 0 if response finished sending; -1 if sending would block OR error occurred.
 * NOTE:
 *  - use message_error() to determine the cause of the error.
 *  - to send a response, response_send() will likely need to be called multiple times
 *    on the same response _res_.
 */
int response_format(httpmsg_t *res);
int response_send(int conn_fd, httpmsg_t *res) {
   ssize_t bytes_sent, bytes_left;

   /* format response if necessary */
   if (res->hm_text == NULL) {
      if (response_format(res) < 0) {
         return -1;
      }
   }

   /* send response (nonblocking) */
   bytes_left = message_textfree(res);
   while (bytes_left > 0) {
      bytes_sent = send(conn_fd, res->hm_text_ptr, bytes_left, MSG_DONTWAIT);
      if (bytes_sent < 0) {
         return -1;
      }
      res->hm_text_ptr += bytes_sent;
      bytes_left -= bytes_sent;
   };
                         
   return 0;
}


/* response_find_status()
 * DESC: convert status code to status phrase.
 * ARGS:
 *  - code: status code (see C_* #defines in webserv-lib.h)
 * RETV: returns pointer to status phrase if found; if not found, returns NULL.
 * ERRS:
 *  - EINVAL: _code_ is not a valid status code.
 */
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
   if (stat_it->phrase == NULL) {
      errno = EINVAL;
      return NULL;
   }
   return stat_it;
}


/* response_insert_header()
 * DESC: constructs header from key-value pair and inserts into response _res_.
 * ARGS:
 *  - key: header key (the part that precedes the colon).
 *  - value: header value (the part that follows the colon, w/o leading space).
 *  - res: response to which the header shall be added.
 * RETV: returns 0 upon success, -1 upon error.
 * NOTE: prints errors.
 */
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


/* response_insert_body()
 * DESC: copy _body_ into the body of the response _res_.
 * ARGS:
 *  - body: pointer to body to copy into _res_.
 *  - bodylen: length of body to copy.
 *  - type: body content type (as string).
 *  - res: pointer to HTTP response
 * RETV: 0 on success, -1 on error.
 */
int response_insert_body(const void *body, size_t bodylen, const char *type, httpmsg_t *res) {
   char *bodylen_str;

   /* resize response's text */
   if (message_resize_body(bodylen, res) < 0) {
      return -1;
   }
   
   /* copy body into response */
   memcpy(res->hm_body, body, bodylen);

   /* set the body pointer to end (indicates response has yet to be written 
    * (or last write was successfully completed) */
   //   res->hm_body_rwp = res->hm_body + bodylen;
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


/* response_insert_line()
 * DESC: create and add HTTP response line into response _res_.
 * ARGS:
 *  - code: status code (C_* constant) of response line.
 *  - version: version of web server.
 *  - res: pointer to HTTP response.
 * RETV: 0 on success, -1 on error.
 */
int response_insert_line(int code, const char *version, httpmsg_t *res) {
   httpres_stat_t *status;
   
   /* match code to response status */
   if ((status = response_find_status(code)) == NULL) {
      return -1;
   }
   res->hm_line.resl.status = status;

   /* copy version into response */
   if ((res->hm_line.resl.version = strdup(version)) == NULL) {
      return -1;
   }

   return 0;
}

/* response_format_headers()
 * DESC: format HTTP response _res_'s headers as sendable string.
 * RETV: pointer to malloc'ed string on success; NULL on error.
 * NOTE: caller needs to free(3) returned non-NULL pointer after use.
 */
char *response_format_headers(httpmsg_t *res) {
   char *hdrs_str, *hdrs_str_it;
   size_t hdrs_str_len, hdrs_str_rem;
   httpmsg_header_t *hdr_it;
   
   if ((hdrs_str = strdup("")) == NULL) {
      return NULL;
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
               return NULL;
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

   return hdrs_str;
}

/* response_format()
 * DESC: format response (formatted buffer is stored internally in response).
 * RETV: 0 on success, -1 on error.
 * NOTE: prints errors.
 */
int response_format(httpmsg_t *res) {
   const char *res_fmt;
   char *hdrs_str, *version;
   const httpres_line_t *line;
   const httpres_stat_t *status;

   /* format headers string */
   if ((hdrs_str = response_format_headers(res)) == NULL) {
      return -1;
   }
   
   /* format & send message */
   res_fmt =
      HM_VERSION_PREFIX"%s %d %s"HM_ENT_TERM   // response line
      "%s"HM_ENT_TERM;                         // response headers
   
   line = &res->hm_line.resl;
   status = line->status;
   version = line->version;
   
   /* calculate total length of message */
   int base_len;
   size_t msg_len;
   base_len = snprintf(NULL, 0, res_fmt, version, status->code, status->phrase, hdrs_str);
   if (base_len < 0) {
      perror("snprintf");
      free(hdrs_str);
      return -1;
   }
   
   /* allocate message */
   msg_len = base_len + res->hm_body_size;
   if ((res->hm_text = malloc(msg_len + 1)) == NULL) { // +1 if no body & need to write '\0'
      perror("malloc");
      free(hdrs_str);
      return -1;
   };
   
   /* copy data into message */
   if (sprintf(res->hm_text, res_fmt, version, status->code, status->phrase, hdrs_str) < 0) {
      perror("sprintf");
      free(hdrs_str);
      return -1;
   }
   if (res->hm_body) {
      memcpy(res->hm_text + base_len, res->hm_body, res->hm_body_size);
   }
   
   /* update response fields */
   res->hm_text_ptr = res->hm_text;
   res->hm_text_size = msg_len;

   if (DEBUG) {
      fprintf(stderr, "formatted response (showing headers only):\n%s", hdrs_str);
   }
   
   free(hdrs_str);
   
   return 0;
}


/* response_insert_file()
 * DESC: add file at path _path_ to response _res_ (as the body).
 * ARGS:
 *  - path: path of file to add.
 *  - res: response to add the file to.
 *  - ftypes: content type table pointer.
 * RETV: returns 0 upon success, returns -1 upon error.
 * NOTE: this is a higher-level function than response_insert_body().
 */
int response_insert_file(const char *path, httpmsg_t *res, const filetype_table_t *ftypes) {
   int fd;
   struct stat fd_info;
   off_t fd_size;
   char *last_mod;
   char *body;
   const char *content_type;
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
   content_type = content_type_get(path, ftypes);
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


/* response_insert_genhdrs()
 * DESC: insert general headers into response.
 * RETV: 0 on success, -1 on error.
 */
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

/* response_insert_servhdrs()
 * DESC: insert server-specific headers into response (HM_HDR_SERVER, HM_HDR_CONNECTION).
 * RETV: 0 on success, -1 on error.
 */
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




