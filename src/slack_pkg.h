#ifndef __SLACK_PKG_H__
#define __SLACK_PKG_H__

#include <stdbool.h>
#include <stdlib.h>

#include <libbds/bds_vector.h>

struct slack_pkg {
        char *name;
        char *version;
        char *arch;
        char *build;
        char *tag;
};

struct slack_pkg slack_pkg_parse(const char *packages_entry);
void slack_pkg_destroy(struct slack_pkg *slack_pkg);

int slack_pkg_compare(const void *a, const void *b);

typedef struct bds_vector *slack_pkg_list_t;

slack_pkg_list_t slack_pkg_list_create();
void slack_pkg_list_destroy(slack_pkg_list_t *spl);

const struct slack_pkg *slack_pkg_list_search_const(const slack_pkg_list_t spl, const char *pkg_name,
                                                    const char *tag);
const struct slack_pkg *slack_pkg_list_get_const(const slack_pkg_list_t spl, size_t i, const char *tag);
void slack_pkg_list_append(slack_pkg_list_t spl, const struct slack_pkg *slack_pkg);
void slack_pkg_list_qsort(slack_pkg_list_t spl);
ssize_t slack_pkg_list_size(const slack_pkg_list_t spl);

#endif
