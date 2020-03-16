#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libbds/bds_string.h>
#include <libbds/bds_vector.h>

#include "slack_pkg.h"

struct slack_pkg slack_pkg_parse(const char *packages_entry)
{
        int rc = 0;
        struct slack_pkg slack_pkg;

        memset(&slack_pkg, 0, sizeof(slack_pkg));

        char *c = NULL;
        // Package format ex: apachetop-0.18.4-x86_64-1_cx
        // name-version-arch-build{tag}
        slack_pkg.name = bds_string_dup(packages_entry);

        if ((c = bds_string_rfind(slack_pkg.name, "-")) == NULL) {
                rc = 1;
                goto finish;
        }
        *c              = '\0';
        slack_pkg.build = c + 1;

        if ((c = bds_string_rfind(slack_pkg.name, "-")) == NULL) {
                rc = 1;
                goto finish;
        }
        *c             = '\0';
        slack_pkg.arch = c + 1;

        if ((c = bds_string_rfind(slack_pkg.name, "-")) == NULL) {
                rc = 1;
                goto finish;
        }
        *c                = '\0';
        slack_pkg.version = c + 1;

        // Take care of the tag
        c = slack_pkg.build;
        while (isdigit(*c))
                ++c;
        slack_pkg.tag = bds_string_dup(c);
        *c            = '\0';

finish:
        if (rc != 0) {
                slack_pkg_destroy(&slack_pkg);
        }

        return slack_pkg;
}

void slack_pkg_destroy(struct slack_pkg *slack_pkg)
{
        if (slack_pkg->name)
                free(slack_pkg->name);
        if (slack_pkg->tag)
                free(slack_pkg->tag);

        memset(slack_pkg, 0, sizeof(*slack_pkg));
}

int slack_pkg_compare(const void *a, const void *b)
{
        return strcmp(((const struct slack_pkg *)a)->name, ((const struct slack_pkg *)b)->name);
}

slack_pkg_list_t slack_pkg_list_create()
{
	return bds_vector_alloc(1, sizeof(struct slack_pkg), (void (*)(void *))slack_pkg_destroy);
}

void slack_pkg_list_destroy(slack_pkg_list_t *spl)
{
	bds_vector_free(spl);
}

const struct slack_pkg *slack_pkg_list_search_const(const slack_pkg_list_t spl, const char *pkg_name, const char *tag)
{
	const struct slack_pkg *rp = NULL;
        const struct slack_pkg *cache_pkg = NULL;
        struct slack_pkg slack_pkg;

        slack_pkg.name = bds_string_dup(pkg_name);
        cache_pkg      = bds_vector_bsearch(spl, &slack_pkg, slack_pkg_compare);

        if (cache_pkg == NULL)
                goto finish;

        if (tag) {
                if (strcmp(cache_pkg->tag, tag) == 0) {
			rp = cache_pkg;
                }
        } else {
		rp = cache_pkg;
        }

finish:
        free(slack_pkg.name);

	return rp;
}

const struct slack_pkg *slack_pkg_list_get_const(const slack_pkg_list_t spl, size_t i, const char *tag)
{
	const struct slack_pkg *cache_pkg = (const struct slack_pkg *)bds_vector_get(spl, i);

	if( tag )
		if( strcmp(cache_pkg->tag, tag) != 0 )
			return NULL;

	return cache_pkg;
}

void slack_pkg_list_append(slack_pkg_list_t spl, const struct slack_pkg *slack_pkg)
{
	bds_vector_append(spl, slack_pkg);
}

void slack_pkg_list_qsort(slack_pkg_list_t spl)
{
	bds_vector_qsort(spl, slack_pkg_compare);
}

ssize_t slack_pkg_list_size(const slack_pkg_list_t spl)
{
	return (ssize_t)bds_vector_size(spl);
}
