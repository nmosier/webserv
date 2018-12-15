#ifndef __WEBSERV_VEC_H
#define __WEBSERV_VEC_H

/* macros */
#define VECTOR_MINLEN 16

#define VECTOR_INIT(vec)             vector_init(vec, sizeof(*(vec)))
#define VECTOR_RESIZE(newlen, vec)   vector_resize(newlen, (void **) &(vec)->arr, &(vec)->cnt,   \
                                                 &(vec)->len, sizeof(*(vec)->arr))
#define VECTOR_REM(vec)              vector_rem((vec)->cnt, (vec)->len)
#define VECTOR_INSERT(elt, vec)      vector_insert(elt, (void **) &(vec)->arr, &(vec)->cnt, \
                                                 &(vec)->len, sizeof(*(vec)->arr))
#define VECTOR_REMOVE(elt, vec, del) vector_remove(elt, (void **) &(vec)->arr, &(vec)->cnt, \
                                                   sizeof(*(vec)->arr), (int (*)(void *)) del)
#define VECTOR_DELETE(vec, del)      vector_delete(vec, (void **) &(vec)->arr, &(vec)->cnt, \
                                                   sizeof(*(vec)), sizeof(*(vec)->arr), \
                                                   (int (*)(void *)) del)
#define VECTOR_QSORT(vec, cmp)       qsort((vec)->arr, (vec)->cnt, sizeof(*(vec)->arr), \
                                           (int (*)(const void *, const void *)) cmp)

/* prototypes */
void vector_init(void *vec, size_t vsize);
int vector_resize(size_t newlen, void **oldarr, size_t *oldcnt, size_t *oldlen, size_t esize);
size_t vector_rem(size_t cnt, size_t len);
int vector_insert(void *elt, void **arr, size_t *cnt, size_t *len, size_t esize);
int vector_remove(size_t i, void **arr, size_t *cnt, size_t esize, int (*del)(void *));
int vector_delete(void *vec, void **arr, size_t *cnt, size_t vsize, size_t esize,
                  int (*del)(void *));


#endif
