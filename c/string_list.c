#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "string_list.h"

static void free_str_ptr(char **str_ptr)
{
	if( *str_ptr ) {
		free(*str_ptr);
	}
	*str_ptr = NULL;
}

int string_list_compar(const void *a, const void *b)
{
        const char *sa = *(const char **)a;
        const char *sb = *(const char **)b;

	return strcmp(sa, sb);
}

string_list_t *string_list_alloc_reference() { return bds_vector_alloc(1, sizeof(char *), NULL); }
string_list_t *string_list_alloc()
{
        return bds_vector_alloc(1, sizeof(char *), (void (*)(void *))free_str_ptr);
}

void string_list_free(string_list_t **string_list) { bds_vector_free(string_list); }

char *string_list_get(string_list_t *string_list, size_t i)
{
	return (char *)string_list_get_const(string_list, i);
}

const char *string_list_get_const(const string_list_t *string_list, size_t i)
{
        const char **p_str = (const char **)bds_vector_get((string_list_t *)string_list, i);

        if (p_str == NULL)
                return NULL;

        return *p_str;
}

size_t string_list_size(const string_list_t *string_list) { return bds_vector_size(string_list); }

void string_list_append(string_list_t *string_list, char *str) { bds_vector_append(string_list, &str); }

void string_list_insert_sort(string_list_t *string_list, char *str)
{
        string_list_append(string_list, str);

        const char **p_str_begin = (const char **)bds_vector_ptr(string_list);
        const char **p_str       = p_str_begin + bds_vector_size(string_list) - 1;

        while (p_str != p_str_begin) {
                if (string_list_compar(p_str - 1, p_str) <= 0) {
                        break;
                }

                const char *tmp = *(p_str - 1);
                *(p_str - 1)                = *p_str;
                *p_str                      = tmp;

                --p_str;
        }
}

int string_list_remove(string_list_t *string_list, const char *str)
{
        char **p_str = (char **)bds_vector_lsearch(string_list, &str, string_list_compar);
        if (p_str == NULL)
                return 1; /* Nothing removed */

	size_t i = p_str - (char **)bds_vector_ptr(string_list);
	bds_vector_remove(string_list, i);

        return 0;
}

void string_list_clear(string_list_t *string_list)
{
	bds_vector_clear(string_list);
}

char *string_list_lsearch(string_list_t *string_list, const char *str)
{
        return (char *)string_list_lsearch_const((const string_list_t *)string_list, str);
}

const char *string_list_lsearch_const(const string_list_t *string_list, const char *str)
{
        const char **p_str = (const char **)bds_vector_lsearch_const(string_list, &str, string_list_compar);
        if (p_str)
                return *p_str;

        return NULL;
}

char *string_list_bsearch(string_list_t *string_list, const char *str)
{
        return (char *)string_list_bsearch_const((const string_list_t *)string_list, str);
}

const char *string_list_bsearch_const(const string_list_t *string_list, const char *str)
{
        const char **p_str = (const char **)bds_vector_bsearch_const(string_list, &str, string_list_compar);
        if (p_str)
                return *p_str;

        return NULL;
}
