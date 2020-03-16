#ifndef __PACKAGES_DB_H__
#define __PACKAGES_DB_H__

#include <stdbool.h>
#include <stdlib.h>

#include "slack_pkg.h"

bool packages_db_is_installed(const char *pkg_name, const char *tag);
const struct slack_pkg *packages_db_search_const(const char *pkg_name, const char *tag);
const struct slack_pkg *packages_db_get_const(size_t i, const char *tag);
ssize_t packages_db_size();

#endif
