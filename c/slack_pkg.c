#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <libbds/bds_string.h>
#include <libbds/bds_vector.h>

#include "slack_pkg.h"

#define SLACK_PKGDB "/var/log/packages"

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

int compar_slack_pkg(const void *a, const void *b)
{
        return strcmp(((const struct slack_pkg *)a)->name, ((const struct slack_pkg *)b)->name);
}

__attribute__((destructor)) static void __fini();

struct bds_vector *pkg_cache = NULL;

static int init_pkg_cache()
{
        struct dirent *dirent = NULL;
        DIR *dp = opendir(SLACK_PKGDB);
        if (dp == NULL) {
                perror("opendir");
                return 1;
        }

        pkg_cache = bds_vector_alloc(1, sizeof(struct slack_pkg), (void (*)(void *))slack_pkg_destroy);

        while ((dirent = readdir(dp)) != NULL) {
                if (dirent->d_type != DT_REG)
                        continue;

                struct slack_pkg slack_pkg = slack_pkg_parse(dirent->d_name);
                if (slack_pkg.name == NULL)
                        continue;

                bds_vector_append(pkg_cache, &slack_pkg);
        }

        if (dp)
                closedir(dp);

        bds_vector_qsort(pkg_cache, compar_slack_pkg);

        return 0;
}

bool slack_pkg_is_installed(const char *pkg_name, const char *tag)
{
        bool rc = false;

        const struct slack_pkg *cache_pkg = NULL;
        struct slack_pkg slack_pkg;

        if (!pkg_cache) {
                if (init_pkg_cache() != 0) {
                        fprintf(stderr, "unable to create slackware package cache\n");
                        return 1;
                }
        }

        slack_pkg.name = bds_string_dup(pkg_name);
        cache_pkg      = bds_vector_bsearch(pkg_cache, &slack_pkg, compar_slack_pkg);

        if (!cache_pkg)
                goto finish;

        if (tag) {
                if (strcmp(cache_pkg->tag, tag) == 0) {
                        rc = true;
                }
        } else {
                rc = true;
        }

finish:
        free(slack_pkg.name);
	return rc;
}

static void __fini()
{
        if (pkg_cache)
                bds_vector_free(&pkg_cache);
}
