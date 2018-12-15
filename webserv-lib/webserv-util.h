#ifndef  __WEBSERV_UTIL_H
#define __WEBSERV_UTIL_H

#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <time.h>
#include "webserv-lib.h"

#define smprintf(sptr, fmt, ...) ((*sptr = malloc(snprintf(NULL, 0, fmt, __VA_ARGS__) + 1)) ? \
                                  ((sprintf(*sptr, fmt, __VA_ARGS__) < 0) ? \
                                   (free(*sptr), -1) : 0) :             \
                                  -1)


typedef struct {
   char *name;
   char *ext;
} filetype_t;

typedef struct {
   filetype_t *arr;
   size_t len;
   size_t cnt;
} filetype_table_t;



char *strstrip(char *str, const char *strip);
char *strrstrip(char *str, const char *strip);
int strprefix(const char *s1, const char *s2);
char *strskip(const char *s1, char *s2);

const char *tm_wday2str(int wday);
const char *tm_mon2str(int mon);

#ifndef PATH_MAX
#define PATH_MAX_RAW pathconf("/", _PC_PATH_MAX)
#define PATH_MAX     (PATH_MAX_RAW < 0) ? 4096 : PATH_MAX_RAW 
#endif


#define HM_FMTDATE_EX  "Thu, 06 Dec 2018 19:57:08 GMT"
#define HM_DATE_LEN    (strlen(HM_FMTDATE_EX)+1)
#define HM_FMTDATE_FMT "%3.3s, %02d %3.3s %04d %02d:%02d:%02d GMT"

int hm_fmtdate(const time_t *sec_ptr, char **time_str);

size_t smin(size_t s1, size_t s2);
size_t smax(size_t s1, size_t s2);

httpreq_method_t hr_str2meth(const char *str);
const char *hr_meth2str(httpreq_method_t meth);

int content_types_init(const char *tabpath, filetype_table_t *ftypes);
int content_types_cmp(const filetype_t *ft1, const filetype_t *ft2);
int content_types_del(filetype_t *ft);


#define CONTENT_TYPE_MAXLEN 128
#define CONTENT_TYPE_PLAIN  "text/plain"
char *get_content_type(const char *path, char *type);


#endif
