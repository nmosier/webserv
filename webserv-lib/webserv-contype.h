#ifndef __WEBSERV_CONTYPE_H
#define __WEBSERV_CONTYPE_H

typedef struct {
   char *name;
   char *ext;
} filetype_t;

typedef struct {
   filetype_t *arr;
   size_t len;
   size_t cnt;
} filetype_table_t;

#define CONTENT_TYPE_PLAIN  "text/plain"
int content_types_load(const char *tabpath, filetype_table_t *ftypes);
void content_type_init(filetype_t *ftype);
int content_type_del(filetype_t *ft);
int content_types_cmp(const filetype_t *ft1, const filetype_t *ft2);
const char *content_type_get(const char *path, const filetype_table_t *ftypes);
int content_types_save(const char *path, const filetype_table_t *ftypes);
void content_types_delete(filetype_table_t *ftypes);

#endif
