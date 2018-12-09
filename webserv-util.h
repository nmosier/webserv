#ifndef  __WEBSERV_UTIL_H
#define __WEBSERV_UTIL_H

#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <time.h>

#define smprintf(sptr, fmt, ...) ((*sptr = malloc(snprintf(NULL, 0, fmt, __VA_ARGS__) + 1)) ? \
                                  ((sprintf(*sptr, fmt, __VA_ARGS__) < 0) ? \
                                      (free(*sptr), -1) : 0) :          \
                                  -1)

char *strstrip(char *str, char *strip);
char *strrstrip(char *str, char *strip);
int strprefix(const char *s1, const char *s2);
char *strskip(const char *s1, char *s2);

const char *tm_wday2str(int wday);
const char *tm_mon2str(int mon);

#ifndef PATH_MAX
#define PATH_MAX_RAW pathconf("/", _PC_PATH_MAX)
#define PATH_MAX     (PATH_MAX_RAW < 0) ? 4096 : PATH_MAX_RAW 
#endif

#define CONTENT_TYPE_MAXLEN 128
#define CONTENT_TYPE_PLAIN  "text/plain"
char *get_content_type(const char *path, char *type);


#define HM_FMTDATE_EX  "Thu, 06 Dec 2018 19:57:08 GMT"
#define HM_DATE_LEN    (strlen(HM_FMTDATE_EX)+1)
#define HM_FMTDATE_FMT "%3.3s, %02d %3.3s %04d %02d:%02d:%02d GMT"

int hm_fmtdate(const time_t *sec_ptr, char **time_str);

size_t smin(size_t s1, size_t s2);
size_t smax(size_t s1, size_t s2);

#endif
