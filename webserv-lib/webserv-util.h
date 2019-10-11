#ifndef  __WEBSERV_UTIL_H
#define __WEBSERV_UTIL_H

#include <stdio.h>
#include <time.h>
#include "webserv-msg.h"

int smprintf(char **sptr, const char *fmt, ...);
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

#endif
