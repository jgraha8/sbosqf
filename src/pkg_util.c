#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libbds/bds_queue.h>
#include <libbds/bds_stack.h>
#include <libbds/bds_string.h>

#include "config.h"
#include "file_mmap.h"
#include "filevercmp.h"
#include "mesg.h"
#include "pkg_ops.h"
#include "pkg_util.h"
#include "sbo.h"
#include "slack_pkg.h"
#include "user_config.h"

void free_string_ptr(char **str)
{
        if (*str == NULL)
                return;
        free(*str);
        *str = NULL;
}

bool check_installed(const struct slack_pkg_dbi *slack_pkg_dbi, const char *pkg_name, struct pkg_options options)
{
        if (options.check_installed) {
                const char *check_tag =
			(options.check_installed & PKG_CHECK_ANY_INSTALLED ? NULL : user_config.sbo_tag);
                return slack_pkg_dbi->is_installed(pkg_name, check_tag);
        }
        return false;
}

bool pkg_is_meta(const char *pkg_name)
{
        bool is_meta = false;

        // Load meta pkg dep file
        char dep_file[2048];
        struct file_mmap *dep = NULL;

        bds_string_copyf(dep_file, sizeof(dep_file), "%s/%s", user_config.depdir, pkg_name);
        if ((dep = file_mmap(dep_file)) == NULL)
                return false;

        char *line     = dep->data;
        char *line_end = NULL;

        while ((line_end = bds_string_find(line, "\n"))) {
                *line_end = '\0';

                if (pkg_skip_dep_line(line))
                        goto cycle;

                if (strcmp(line, "METAPKG") == 0) {
                        is_meta = true;
                        break;
                }
        cycle:
                line = line_end + 1;
        }
        file_munmap(&dep);

        return is_meta;
}

bool pkg_skip_dep_line(char *line)
{
        // Trim newline
        char *c = line;
        while (*c) {
                if (*c == '\n' || *c == '\t' || *c == '\\') {
                        *c = ' ';
                }
                ++c;
        }

        // Trim comments
        c = bds_string_find(line, "#");
        if (c) {
                *c = '\0';
        }

        if (*bds_string_atrim(line) == '\0') {
                return true;
        }

        if (*line == '-') {
                return true;
        }

        return false;
}

bool file_exists(const char *pathname)
{
        struct stat sb;

        if (stat(pathname, &sb) == -1)
                return false;

        if (!S_ISREG(sb.st_mode))
                return false;

        return true;
}

bool pkg_dep_file_exists(const char *pkg_name)
{
        char dep_file[4096];

        bds_string_copyf(dep_file, sizeof(dep_file), "%s/%s", user_config.depdir, pkg_name);

        return file_exists(dep_file);
}

const char *create_default_dep(const struct pkg *pkg)
{
        if (pkg->sbo_dir == NULL)
                return NULL;

        static char dep_file[4096];

        FILE *fp            = NULL;
        char **required     = NULL;
        size_t num_required = 0;

        const char *rp = NULL;

        const char *sbo_requires = sbo_read_requires(pkg->sbo_dir, pkg->name);
        if (!sbo_requires) {
                goto finish;
        }

        bds_string_copyf(dep_file, sizeof(dep_file), "%s/%s", user_config.depdir, pkg->name);

        fp = fopen(dep_file, "w");
        if (fp == NULL) {
                perror("fopen");
                goto finish;
        }
        fprintf(fp, "REQUIRED:\n");

        bds_string_tokenize((char *)sbo_requires, " ", &num_required, &required);
        for (size_t i = 0; i < num_required; ++i) {
                if (required[i] == NULL)
                        continue;
                if (bds_string_atrim(required[i]) == 0)
                        continue;
                if (strcmp(required[i], "%README%") == 0)
                        continue;

                fprintf(fp, "%s\n", required[i]);
        }

        fprintf(fp, "\nOPTIONAL:\n");
        fprintf(fp, "\nBUILDOPTS:\n");

        rp = dep_file;
finish:
        if (fp)
                fclose(fp);
        if (required)
                free(required);

        return rp;
}

const char *create_default_dep_verbose(const struct pkg *pkg)
{
        const char *dep_file = NULL;
        if ((dep_file = create_default_dep(pkg)) != NULL) {
                mesg_info("created %s\n", dep_file);
        } else {
                mesg_error("unable to create %s dependency file\n", pkg->name);
        }
        return dep_file;
}

/*
 * Return:
 *   -1   if error occurred
 *    0   success / no errors
 *    >0  dep file has been modified, during review (1 == PKG_DEP_REVERTED_DEFAULT, 2 == PKG_DEP_EDITED)
 */

int pkg_check_reviewed(struct pkg *pkg, enum pkg_review_type review_type, bool *db_dirty)
{
        int rc = 0;

        if (review_type == PKG_REVIEW_DISABLED)
                return 0;

        if (pkg->is_reviewed)
                return 0;

        int rc_review = -1;
        switch (review_type) {
        case PKG_REVIEW_DISABLED:
                mesg_error("internal error: review type should not be PKG_REVIEW_DISABLED\n");
                abort();
                break;
        case PKG_REVIEW_AUTO:
                rc_review = 0; /* Set the add-to-REVIEWED status and proceed */
                break;
        case PKG_REVIEW_AUTO_VERBOSE:
                rc_review = pkg_review(pkg);
                break;
        default: // PKG_REVIEW_ENABLED
                /* Use the dep status as the return code */
                rc_review = pkg_review_prompt(pkg, PKG_DEP_REVERTED_DEFAULT, &rc);
                if (rc_review < 0) { /* If an error occurs set error return code */
                        rc = -1;
                }
                break;
        }

        if (rc_review == 0) { // Add-to-REVIEWED status
                pkg->is_reviewed = true;
                *db_dirty        = true;
        }

        return rc;
}



int pkg_compare_versions(const char *ver_a, const char *ver_b) { return filevercmp(ver_a, ver_b); }
