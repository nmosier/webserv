#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include "webserv-util.h"
#include "webserv-dbg.h"

/* smprintf()
 * DESC: format string into dynamically allocated string -- s(tring)m(alloc)printf(ormatted)
 * ARGS:
 *  - sptr: pointer to location at which to store allocated string.
 *  - fmt: format string (see sprintf(3))
 *  - ...: format arguments (see sprintf(3))
 * RETV: see sprintf(3)
 * ERRS: see sprintf(3)
 * NOTE: _*sptr_ is only modified and string is only allocated (and thus needs to bee free(3)ed
 *       if smprintf() is successful.
 */
int smprintf(char **sptr, const char *fmt, ...) {
   va_list args;
   int strlen;
   char *str;

   /* get formatted string length */
   va_start(args, fmt);
   if ((strlen = vsnprintf(NULL, 0, fmt, args)) < 0) {
      return -1;
   }
   va_end(args);

   /* allocate buffer for formatted string */
   if ((str = malloc(strlen + 1)) == NULL) { // note: count + 1 > 0
      return -1;
   }

   /* format string */
   va_start(args, fmt);
   if ((vsnprintf(str, strlen + 1, fmt, args)) > strlen) {
      free(str);
      return -1;
   }
   va_end(args);

   *sptr = str;
   return strlen;
}

/* strstrip()
 * DESC: strip any leading characters of _str_ that occur in _strip_
 * ARGS:
 *  - str: string to "strip" the leading characters off of
 *  - strip: list of characters to strip
 * RETV: returns pointer to first character in _str_ that does not occur in _strip_
 * EXAMPLE: strstrip("abbaccc", "ab") -> "ccc"
 */
char *strstrip(char *str, const char *strip) {
   while (*str != '\0' && strchr(strip, *str)) {
      ++str;
   }
   return str;
}

/* strrstrip() 
 * DESC: strip any trailing characters of _str_ that occur in _strip_
 *       (note: modifies _str_ by writing '\0' at end of stripped result)
 * ARGS:
 *  - str: string to "strip" the trailing characters off of
 *  - strip: list of characters to strip
 * RETV: returns _str_
 */
char *strrstrip(char *str, const char *strip) {
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

/* tm_wday2str()
 * DESC: converts _wday_ to string (weekday abbreviation)
 */
const char *tm_wday2str(int wday) {
   const char *wdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
   return wdays[wday];
}

/* tm_mon2str()
 * DESC: converts _mon_ to string (month abbreviation 
 */
const char *tm_mon2str(int mon) {
   const char *mons[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
   return mons[mon];
}

/* hm_fmtdate()
 * DESC: formats date in HTTP date format given time in seconds, _sec_ptr_.
 * ARGS:
 *  - sec_ptr: pointer to time_t value to format.
 *  - time_str: pointer to where the dynamically-allocated formatted string should be placed.
 * RETV: returns 0 upon success; returns -1 upon error. Upon error, no string is allocated.
 * NOTE: Since this function dynamically allocates a string on success, the programmer must 
 *       delete it after use.
 */
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

/* smax()
 * DESC: return the maximum of two size_t values.
 */
size_t smax(size_t s1, size_t s2) {
   return (s1 < s2) ? s2 : s1;
}

/* smin()
 * DESC: return the maximum of two size_t values.
 */
size_t smin(size_t s1, size_t s2) {
   return (s1 < s2) ? s1 : s2;
}

/* hr_str2meth()
 * DESC: converts HTTP method string _str_ to enum type httpreq_method_t (values are M_*).
 * ARGS:
 *  - str: HTTP method string to convert.
 * RETV: returns method's enum representation (M_*) upon success, -1 upon error.
 * ERRS:
 *  - EINVAL: _str_ does not represent a valid HTTP mode or is not supported.
 */
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
   errno = EINVAL;
   return -1;
}

/* hr_str2meth()
 * DESC: converts HTTP method enum value (M_*) to string representation.
 * ARGS:
 *  - meth: HTTP method in enum (M_*) representation.
 * RETV: returns string representation on success, NULL on error;
 * ERRS:
 *  - EINVAL: _meth_ does not represent a valid HTTP mode or is not supported.
 */
const char *hr_meth2str(httpreq_method_t meth) {
   for (hr_str2meth_t *it = hr_str2meth_v; it->str; ++it) {
      if (meth == it->meth) {
         return it->str;
      }
   }
   return NULL;
}







