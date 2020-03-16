#ifndef __SLACK_PKG_DBI_H__
#define __SLACK_PKG_DBI_H__

#include "slack_pkg.h"

enum slack_pkg_dbi_type { SLACK_PKG_DBI_NONE=0, SLACK_PKG_DBI_PACKAGES, SLACK_PKG_DBI_REPO };

struct slack_pkg_dbi {
        bool (*is_installed)(const char *pkg_name, const char *tag);
        const struct slack_pkg *(*search_const)(const char *pkg_name, const char *tag);
        const struct slack_pkg *(*get_const)(size_t i, const char *tag);
        ssize_t (*size)();

	enum slack_pkg_dbi_type dbi_type;
};

struct slack_pkg_dbi slack_pkg_dbi_create(enum slack_pkg_dbi_type dbi_type );
void slack_pkg_dbi_destroy(struct slack_pkg_dbi *spd);

#endif
