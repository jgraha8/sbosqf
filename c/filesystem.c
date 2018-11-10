#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <libbds/bds_string.h>

#include "file_mmap.h"
#include "filesystem.h"

#define SLACK_PKGDB "/var/log/packages"

const char *find_sbo_dir(const char *sbo_repo, const char *pkg_name)
{
        static char sbo_dir[4096];
        static int level = 0;

        const char *rp        = NULL;
        struct dirent *dirent = NULL;

        DIR *dp = opendir(sbo_repo);
        if (dp == NULL)
                return NULL;

        ++level;
        assert(level >= 1);

        if (level > 2) {
                goto finish;
        }

        while ((dirent = readdir(dp)) != NULL) {
                if (strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0)
                        continue;

                if (level == 1) {
                        if (dirent->d_type == DT_DIR) {
                                char *next_dir = bds_string_dup_concat(3, sbo_repo, "/", dirent->d_name);
                                rp             = find_sbo_dir(next_dir, pkg_name);
                                free(next_dir);
                                if (rp) {
                                        goto finish;
                                }
                        }
                } else {
                        if (dirent->d_type == DT_DIR) {
                                if (strcmp(dirent->d_name, pkg_name) == 0) {
                                        rp = bds_string_copyf(sbo_dir, 4096, "%s/%s", sbo_repo, dirent->d_name);
                                        goto finish;
                                }
                        }
                }
        }

finish:
        if (dp)
                closedir(dp);
        --level;

        return rp;
}

const char *find_sbo_info(const char *sbo_repo, const char *pkg_name)
{
        static char info_file[4096];
        const char *sbo_dir = find_sbo_dir(sbo_repo, pkg_name);

        if (!sbo_dir)
                return NULL;

        bds_string_copyf(info_file, sizeof(info_file), "%s/%s.info", sbo_dir, pkg_name);

        return info_file;
}

const char *find_sbo_readme(const char *sbo_repo, const char *pkg_name)
{
        static char readme_file[4096];
        const char *sbo_dir = find_sbo_dir(sbo_repo, pkg_name);

        if (!sbo_dir)
                return NULL;

        bds_string_copyf(readme_file, sizeof(readme_file), "%s/README", sbo_dir);

        return readme_file;
}

const char *read_sbo_requires(const char *sbo_dir, const char *pkg_name)
{
        static char sbo_requires[4096];
        const char *rp = NULL;

        char *info_file = bds_string_dup_concat(4, sbo_dir, "/", pkg_name, ".info");

        struct file_mmap *info;
        if ((info = file_mmap(info_file)) == NULL) {
                goto finish;
        }

        char *c = bds_string_find(info->data, "REQUIRES=");
        if (c == NULL) {
                goto finish;
        }
        char *str = c + strlen("REQUIRES=");

        // Add terminator
        if ((c = bds_string_find(str, "\n"))) {
                *c = '\0';
        }

        // Remove quotes
        while ((c = bds_string_find(str, "\""))) {
                *c = ' ';
        }
        strncpy(sbo_requires, bds_string_atrim(str), 4096);
        rp = sbo_requires;

finish:
        if (info_file)
                free(info_file);
        if (info)
                file_munmap(&info);

        return rp;
}

struct slack_pkg parse_slack_pkg(const char *pkgdb_entry)
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
                destroy_slack_pkg(&slack_pkg);
        }

        return slack_pkg;
}

void destroy_slack_pkg(struct slack_pkg *slack_pkg)
{
        if (slack_pkg->name)
                free(slack_pkg->name);
        if (slack_pkg->tag)
                free(slack_pkg->tag);

        memset(slack_pkg, 0, sizeof(*slack_pkg));
}

bool is_pkg_installed(const char *pkg_name, const char *tag)
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

                struct slack_pkg slack_pkg = parse_slack_pkg(dirent->d_name);
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
                destroy_slack_pkg(&slack_pkg);
        }

        if (dp)
                closedir(dp);

        return rc;
}
