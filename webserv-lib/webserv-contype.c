#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "webserv-util.h"
#include "webserv-vec.h"
#include "webserv-dbg.h"
#include "webserv-contype.h"

int content_types_load(const char *tabpath, filetype_table_t *ftypes) {
   int tabfd;
   off_t tablen;
   struct stat tabstat;
   char *tab;
   int retv, errsav;
   
   /* initialize vars */
   tabfd = -1;
   tab = NULL;
   retv = -1;
   VECTOR_INIT(ftypes);
   
   /* open type table file */
   if ((tabfd = open(tabpath, O_RDONLY)) < 0) {
      goto cleanup;
   }

   /* get length of type table */
   if (fstat(tabfd, &tabstat) < 0) {
      goto cleanup;
   }
   tablen = tabstat.st_size;

   /* map types filedes into memory */
   if ((tab = mmap(NULL, tablen, PROT_READ|PROT_WRITE, MAP_PRIVATE, tabfd, 0)) == NULL) {
      goto cleanup;
   }

   /* tokenize file's contents and store into vector */
   char *name, *ext, *line;
   char *line_last; // for strtok_r()'s parsing of lines
   const char *line_term = "\n", *name_term = " \t", *ext_term = " \t";
   filetype_t ftype;

   if ((line = strtok_r(tab, line_term, &line_last))) {
      line = strstrip(line, line_term);
   }
   while (line && *line == '#') {
      if ((line = strtok_r(NULL, line_term, &line_last))) {
         line = strstrip(line, line_term);
      }
   }
   while (line && isgraph(*line)) {
      /* parse name */
      if ((name = strtok(line, name_term)) == NULL || !isgraph(*name)) {
         errno = EINVAL;
         goto cleanup;
      }
      
      /* parse all extensions (if any) */
      while ((ext = strtok(NULL, ext_term))) {
         /* strip leading spaces */
         ext = strstrip(ext, name_term);
         
         /* validate extension */
         if (!isgraph(*ext)) {
            errno = EINVAL;
            goto cleanup;
         }
         
         /* dupe name & extension */
         content_type_init(&ftype);
         if ((ftype.name = strdup(name)) == NULL) {
            goto cleanup;
         }
         if ((ftype.ext = strdup(ext)) == NULL) {
            content_type_del(&ftype);
            goto cleanup;
         }
         
         /* insert (name, ext.) pair into table */
         if (VECTOR_INSERT(&ftype, ftypes) < 0) {
            content_type_del(&ftype);
            goto cleanup;
         }
      }
      
      /* read in next line */
      do {
         if ((line = strtok_r(NULL, line_term, &line_last))) {
            line = strstrip(line, line_term);
         }
      } while (line && *line == '#');
   }
   
   /* sort table (by extension) */
   VECTOR_QSORT(ftypes, content_types_cmp);

   retv = 0; // success

   /* cleanup */
 cleanup:
   errsav = errno; // save error, if any
   if (tab && munmap(tab, tablen) < 0) {
      if (retv >= 0) {
         errsav = errno;
      }
      retv = -1;
   }
   if (tabfd >= 0 && close(tabfd) < 0) {
      if (retv >= 0){
         errsav = errno;
      }
      retv = -1;
   }
   if (retv < 0) {
      VECTOR_DELETE(ftypes, content_type_del); // always succeeds
   }

   errno = errsav;
   return retv;
}

   void content_type_init(filetype_t *ftype) {
      memset(ftype, 0, sizeof(*ftype));
   }
   
int content_types_cmp(const filetype_t *ft1, const filetype_t *ft2) {
   return strcmp(ft1->ext, ft2->ext);
}

int content_type_del(filetype_t *ft) {
   free(ft->name);
   free(ft->ext);
   return 0; // always succeeds
}


const char *content_type_get(const char *path, const filetype_table_t *ftypes) {
   char *name, *ext;
   filetype_t key, *match;

   /* parse extension */
   if ((ext = strrchr(path, '.'))) {
      ext += 1; // skip over leading '.' of extension
      
      /* find extension in file type table */
      key.ext = ext;
      match = bsearch(&key, ftypes->arr, ftypes->cnt, sizeof(*ftypes->arr),
                     (int (*) (const void *, const void *)) content_types_cmp);
   }

   name = match ? match->name : CONTENT_TYPE_PLAIN;

   if (DEBUG) {
      fprintf(stderr, "(name, ext) = (%s, %s)\n", name, ext);
   }

   return name;
}

int content_types_save(const char *path, const filetype_table_t *ftypes) {
   FILE *file;
   size_t i;
   filetype_t *ftype;
   int retv, errsav;

   retv = 0;

   if ((file = fopen(path, "w")) == NULL) {
      return -1;
   }

   for (i = 0, ftype = ftypes->arr; i < ftypes->cnt; ++i, ++ftype) {
      if (fprintf(file, "%s\t\t\t%s\n", ftype->name, ftype->ext) < 0) {
         retv = -1;
         errsav = errno;
         break;
      }
   }

   /* cleanup */
   if (fclose(file) < 0 && retv >= 0) {
      retv = -1;
      errsav = errno;
   }
   if (retv < 0) {
      errno = errsav;
   }
   
   return retv;
}

void content_types_delete(filetype_table_t *ftypes) {
   VECTOR_DELETE(ftypes, content_type_del);
}
