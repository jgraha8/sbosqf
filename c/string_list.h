#ifndef __STRING_LIST_H__
#define __STRING_LIST_H__

#include <libbds/bds_vector.h>

typedef struct bds_vector string_list_t;

string_list_t *string_list_alloc_reference();
string_list_t *string_list_alloc();

size_t string_list_size(const string_list_t *string_list);
char *string_list_get(string_list_t *string_list, size_t i);
const char *string_list_get_const(const string_list_t *string_list, size_t i);

void string_list_free(string_list_t **string_list);
void string_list_append(string_list_t *string_list, char *pkg);
void string_list_insert_sort(string_list_t *string_list, char *pkg);
int string_list_remove(string_list_t *string_list, const char *pkg_name);
void string_list_clear(string_list_t *string_list);
char *string_list_lsearch(string_list_t *string_list, const char *name);
const char *string_list_lsearch_const(const string_list_t *string_list, const char *name);
char *string_list_bsearch(string_list_t *string_list, const char *name);
const char *string_list_bsearch_const(const string_list_t *string_list, const char *name);
int string_list_compar(const void *a, const void *b);

#endif
