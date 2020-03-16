#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slack_pkg_dbi.h"
#include "packages_db.h"
#include "slackpkg_repo.h"

struct slack_pkg_dbi slack_pkg_dbi_create(enum slack_pkg_dbi_type dbi_type)
{
        struct slack_pkg_dbi spd = {.dbi_type = dbi_type};

        switch (dbi_type) {
        case SLACK_PKG_DBI_PACKAGES:
                spd.is_installed = packages_db_is_installed;
                spd.search_const = packages_db_search_const;
                spd.get_const    = packages_db_get_const;
                spd.size         = packages_db_size;
                break;
        case SLACK_PKG_DBI_REPO:
                spd.is_installed = slackpkg_repo_is_installed;
                spd.search_const = slackpkg_repo_search_const;
                spd.get_const    = slackpkg_repo_get_const;
                spd.size         = slackpkg_repo_size;
                break;
        default:
                fprintf(stderr, "slack_pkg_dbi: DBI type not specified\n");
                exit(EXIT_FAILURE);
        }

        return spd;
}

void slack_pkg_dbi_destroy(struct slack_pkg_dbi *spd) { memset(spd, 0, sizeof(*spd)); }
