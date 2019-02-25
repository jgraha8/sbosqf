#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <libbds/bds_string.h>

#include "slack_pkg.h"

#define SLACK_PKGDB "/var/log/packages"

struct slack_pkg slack_pkg_parse(const char *pkgdb_entry)
{
        int rc = 0;
        struct slack_pkg slack_pkg;

        memset(&slack_pkg, 0, sizeof(slack_pkg));

        char *c = NULL;
        // Package format ex: apachetop-0.18.4-x86_64-1_cx
        // name-version-arch-build{tag}
        slack_pkg.name = bds_string_dup(pkgdb_entry);

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

bool slack_pkg_is_installed(const char *pkg_name, const char *tag)
{
        DIR *dp = opendir(SLACK_PKGDB);
        if (dp == NULL) {
                perror("opendir");
                return false;
        }

        bool rc               = false;
        bool do_check         = true;
        struct dirent *dirent = NULL;

        while (do_check && (dirent = readdir(dp)) != NULL) {
                if (dirent->d_type != DT_REG)
                        continue;

                struct slack_pkg slack_pkg = slack_pkg_parse(dirent->d_name);
                if (slack_pkg.name == NULL)
                        continue;

                if (strcmp(slack_pkg.name, pkg_name) == 0) {
                        if (tag) {
                                if (strcmp(slack_pkg.tag, tag) == 0) {
                                        rc = true;
                                }
                        } else {
                                rc = true;
                        }
                        do_check = false;
                }
                slack_pkg_destroy(&slack_pkg);
        }

        if (dp)
                closedir(dp);

        return rc;
}
