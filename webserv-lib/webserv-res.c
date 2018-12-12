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

/*************** HTTP RESPONSE FUNCTIONS ***************/
void response_delete(httpmsg_t *res) {
   message_delete(res);

   /* delete response members */
   if (res) {
      free(res->hm_line.resl.version);
   }
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


// NOTE: body_rwp is set to BEGINNING; this is necessary for response_sned() to work
int response_insert_body(const char *body, size_t bodylen, const char *type, httpmsg_t *res) {
   char *bodylen_str;

   printf("body=%s\n", body);
   
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


// upon first call, res->hm_body_rwp == res->hm_body_size;
// allows for repeated sending!
int response_send(int conn_fd, httpmsg_t *res) {
   const char *res_fmt;
   char *hdrs_str, *hdrs_str_it, *version;
   size_t hdrs_str_len, hdrs_str_rem;
   httpmsg_header_t *hdr_it;
   const httpres_line_t *line;
   const httpres_stat_t *status;
   ssize_t bytes_sent;

   //printf("BODY=%s\n", res->hm_body);

   if (res->hm_text == NULL) {
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
   
   //send_status = dprintf(conn_fd, res_fmt,
   //                         version, status->code, status->phrase,       // line args
   //                         hdrs_str);                                   // header arg

   free(hdrs_str);
   /*
   if (send_status < 0) {
      perror("dprintf");
      return -1;
   }
   */

   }   

   size_t bytes_left = res->hm_text_size;
   while (bytes_left > 0) {
      bytes_sent = send(conn_fd, res->hm_text_ptr, bytes_left, 0); // TODO
      if (bytes_sent <= 0) {
         return -1;
      }
      res->hm_text_ptr += bytes_sent;
      bytes_left -= bytes_sent;
   };
                         
   return 0;
}


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




