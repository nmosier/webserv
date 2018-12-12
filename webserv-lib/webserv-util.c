#include <string.h>
#include <stdio.h>
#include <sys/utsname.h>
#include "webserv-lib.h"
#include "webserv-util.h"
#include "webserv-dbg.h"

char *strstrip(char *str, char *strip) {
   while (*str && strchr(strip, *str)) {
      ++str;
   }
   return str;
}

char *strrstrip(char *str, char *strip) {
   char *str_it;
   for (str_it = strchr(str, '\0'); str_it > str && strchr(strip, str[-1]); --str_it) {}
   *str_it = '\0';
   return str;
}

/* strprefix()
 * DESC: determines whether string s1 is a prefix of string s2.
 * ARGS:
 *  - s1: prefix string
 *  - s2: full string
 * RETV: returns 1 if s1 is a prefix of s2; returns 0 otherwise (including when
 *       either string is NULL)
 */
int strprefix(const char *s1, const char *s2) {
   return s1 && s2 && !strncmp(s1, s2, strlen(s1));
}

/* strskip()
 * DESC: matches & skips over prefix string s1 of s2.
 * ARGS:
 *  - s1: prefix string to skip over
 *  - s2: full string
 * RETV: returns pointer in s2 after occurrence of prefix s1; if s1 is NOT a prefix
 *       of s2 (as determined by strprefix()), return NULL
 */
char *strskip(const char *s1, char *s2) {
   return strprefix(s1, s2) ? s2 + strlen(s1) : NULL;
}


const char *tm_wday2str(int wday) {
   const char *wdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
   return wdays[wday];
}

const char *tm_mon2str(int mon) {
   const char *mons[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
   return mons[mon];
}

// formats date for HTTP
int hm_fmtdate(const time_t *sec_ptr, char **time_str) {
   struct tm *time_info;
   
   if ((time_info = gmtime(sec_ptr)) == NULL) {
      return -1;
   }

   if (smprintf(time_str,
                "%3.3s, %02d %3.3s %04d %02d:%02d:%02d GMT",
                tm_wday2str(time_info->tm_wday),       time_info->tm_mday,
                tm_mon2str(time_info->tm_mon),         time_info->tm_year + 1900,
                time_info->tm_hour, time_info->tm_min, time_info->tm_sec)
       < 0) {
      return -1;
   }
   
   return 0;
}


char *get_content_type(const char *path, char *type) {
   const char *filename;
   const char *extension;

   if ((filename = strrchr(path, '/')) == NULL) {
      filename = path;
   }

   if ((extension = strrchr(path, '.')) == NULL) {
      extension = CONTENT_TYPE_PLAIN;
   }

   return strcpy(type, extension);
}


size_t smax(size_t s1, size_t s2) {
   return (s1 < s2) ? s2 : s1;
}

size_t smin(size_t s1, size_t s2) {
   return (s1 < s2) ? s1 : s2;
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

const char *hr_meth2str(httpreq_method_t meth) {
   for (hr_str2meth_t *it = hr_str2meth_v; it->str; ++it) {
      if (meth == it->meth) {
         return it->str;
      }
   }
   return NULL;
}
