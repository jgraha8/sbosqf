#ifndef __SLACK_PKG_H__
#define __SLACK_PKG_H__

struct slack_pkg {
        char *name;
        char *version;
        char *arch;
        char *build;
        char *tag;
};


struct slack_pkg slack_pkg_parse(const char *packages_entry);
void slack_pkg_destroy(struct slack_pkg *slack_pkg);
bool slack_pkg_is_installed(const char *pkg_name, const char *tag);
const struct slack_pkg *slack_pkg_search_const(const char *pkg_name, const char *tag);
#endif
