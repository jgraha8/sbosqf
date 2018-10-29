#ifndef __FILESYSTEM_H__
#define __FILESYSTEM_H__

struct slack_pkg {
        char *name;
        char *version;
        char *arch;
        char *build;
        char *tag;
};

struct slack_pkg parse_slack_pkg(const char *pkgdb_entry);

void destroy_slack_pkg(struct slack_pkg *slack_pkg);

const char *find_sbo_dir(const char *dir_name, const char *pkg_name);

const char *read_sbo_requires(const char *sbo_dir, const char *pkg_name);

bool is_pkg_installed(const char *pkg_name, const char *tag);

#endif // __FILESYSTEM_H__
