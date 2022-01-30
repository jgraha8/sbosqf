#define _DEFAULT_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "packages_db.h"

#define SLACK_PKGDB "/var/log/packages"

__attribute__((destructor)) static void __fini();

static slack_pkg_list_t pkg_cache = NULL;

static int init_pkg_cache()
{
        struct dirent *dirent = NULL;
        DIR *dp = opendir(SLACK_PKGDB);
        if (dp == NULL) {
                perror("opendir");
                return 1;
        }

        pkg_cache = slack_pkg_list_create();

	while ((dirent = readdir(dp)) != NULL) {
                if (dirent->d_type != DT_REG)
                        continue;

                struct slack_pkg slack_pkg = slack_pkg_parse(dirent->d_name);
                if (slack_pkg.name == NULL)
                        continue;

                slack_pkg_list_append(pkg_cache, &slack_pkg);
        }

        if (dp)
                closedir(dp);

        slack_pkg_list_qsort(pkg_cache);

        return 0;
}

const struct slack_pkg *packages_db_search_const(const char *pkg_name, const char *tag)
{
        if (!pkg_cache) {
                if (init_pkg_cache() != 0) {
                        fprintf(stderr, "unable to create slackware package cache\n");
                        return NULL;
                }
        }

	return slack_pkg_list_search_const(pkg_cache, pkg_name, tag);
}

bool packages_db_is_installed(const char *pkg_name, const char *tag)
{
	return (packages_db_search_const(pkg_name, tag) != NULL );
}

ssize_t packages_db_size()
{
        if (!pkg_cache) {
                if (init_pkg_cache() != 0) {
                        fprintf(stderr, "unable to create slackware package cache\n");
                        return -1;
                }
        }
	return slack_pkg_list_size(pkg_cache);
}

const struct slack_pkg *packages_db_get_const(size_t i, const char *tag)
{
	if (!pkg_cache) {
                if (init_pkg_cache() != 0) {
                        fprintf(stderr, "unable to create slackware package cache\n");
                        return NULL;
                }
        }

	return slack_pkg_list_get_const(pkg_cache, i, tag);
}



static void __fini()
{
        if (pkg_cache) {
		slack_pkg_list_destroy(&pkg_cache);
	}
}
