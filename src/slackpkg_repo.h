#ifndef __SLACKPKG_REPO_H__
#define __SLACKPKG_REPO_H__

#include "slack_pkg.h"

bool slackpkg_repo_is_installed(const char *pkg_name, const char *tag);
const struct slack_pkg *slackpkg_repo_search_const(const char *pkg_name, const char *tag);
const struct slack_pkg *slackpkg_repo_get_const(size_t i, const char *tag);
ssize_t slackpkg_repo_size();

#endif
