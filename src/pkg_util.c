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

enum block_type { NO_BLOCK, REQUIRED_BLOCK, OPTIONAL_BLOCK, BUILDOPTS_BLOCK };

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

static bool skip_dep_line(char *line)
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

bool is_meta_pkg(const char *pkg_name)
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

                if (skip_dep_line(line))
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



bool file_exists(const char *pathname)
{
        struct stat sb;

        if (stat(pathname, &sb) == -1)
                return false;

        if (!S_ISREG(sb.st_mode))
                return false;

        return true;
}

bool dep_file_exists(const char *pkg_name)
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

static void write_buildopts(struct ostream *os, const struct pkg *pkg)
{
        const size_t n = pkg_buildopts_size(pkg);

        if (0 < n) {
                ostream_printf(os, " |");
        }
        for (size_t i = 0; i < n; ++i) {
                ostream_printf(os, " %s", pkg_buildopts_get_const(pkg, i));
        }
}

/*
  Return:
    0    always
 */
static int check_track_pkg(struct pkg *pkg, int node_dist, enum pkg_track_mode track_mode, bool *db_dirty)
{
        if ((PKG_TRACK_ENABLE == track_mode && 0 == node_dist) || PKG_TRACK_ENABLE_ALL == track_mode) {
                pkg->is_tracked = true;
                *db_dirty       = true;
        }
        return 0;
}

/*
 * Return:
 *   -1   if error occurred
 *    0   success / no errors
 *    >0  dep file has been modified, during review (1 == PKG_DEP_REVERTED_DEFAULT, 2 == PKG_DEP_EDITED)
 */

int check_reviewed_pkg(struct pkg *pkg, enum pkg_review_type review_type, bool *db_dirty)
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

/*
 * Return:
 *   -1   if error occurred
 *    0   success / no errors
 *    >0  dep file has been modified, during review (1 == PKG_DEP_REVERTED_DEFAULT, 2 == PKG_DEP_EDITED)
 */
static int __write_sqf(struct pkg_graph *pkg_graph, const struct slack_pkg_dbi *slack_pkg_dbi,
                       const char *pkg_name, struct pkg_options options, bool *db_dirty,
                       pkg_nodes_t *review_skip_pkgs, pkg_nodes_t *output_pkgs)
{
        int rc = 0;
        struct pkg_iterator iter;

        pkg_iterator_flags_t flags = 0;
        int max_dist               = (options.max_dist >= 0 ? options.max_dist : (options.deep ? -1 : 1));

        if (options.revdeps) {
                flags = PKG_ITER_REVDEPS;
        } else {
                flags = PKG_ITER_DEPS;
        }

        for (struct pkg_node *node = pkg_iterator_begin(&iter, pkg_graph, pkg_name, flags, max_dist); node != NULL;
             node                  = pkg_iterator_next(&iter)) {

                if (node->pkg.dep.is_meta)
                        continue;

                check_track_pkg(&node->pkg, node->dist, options.track_mode, db_dirty);

                if (options.check_installed && strcmp(pkg_name, node->pkg.name) != 0) {
                        const char *tag =
                            (options.check_installed & PKG_CHECK_ANY_INSTALLED ? NULL : user_config.sbo_tag);
                        if (slack_pkg_dbi->is_installed(node->pkg.name, tag)) {
                                // mesg_info("package %s is already installed: skipping\n", node->pkg.name);
                                continue;
                        }
                }

                if (pkg_nodes_bsearch_const(review_skip_pkgs, node->pkg.name) == NULL) {
                        rc = check_reviewed_pkg(&node->pkg, options.review_type, db_dirty);
                        if (rc < 0) {
                                goto finish;
                        }
                        if (rc > 0) {
                                // Package dependency file was edited (it may have been updated): reload the dep
                                // file and process the node
                                pkg_clear_required(&node->pkg);
                                pkg_load_dep(pkg_graph, node->pkg.name, options);
                                goto finish;
                        }
                        pkg_nodes_insert_sort(review_skip_pkgs, node);
                }

                if (pkg_nodes_lsearch_const(output_pkgs, node->pkg.name) == NULL) {
                        pkg_nodes_append(output_pkgs, node);
                }
        }

finish:
        pkg_iterator_destroy(&iter);

        return rc;
}


int compar_versions(const char *ver_a, const char *ver_b) { return filevercmp(ver_a, ver_b); }

const char *find_dep_file(const char *pkg_name)
{
        static char dep_file[4096];
        struct stat sb;

        bds_string_copyf(dep_file, sizeof(dep_file), "%s/%s", user_config.depdir, pkg_name);

        if (stat(dep_file, &sb) == 0) {
                if (sb.st_mode & S_IFREG) {
                        return dep_file;
                } else if (sb.st_mode & S_IFDIR) {
                        fprintf(stderr, "dependency file for package %s already exists as directory: %s\n",
                                pkg_name, dep_file);
                } else {
                        fprintf(stderr, "dependency file for package %s already exists as non-standard file: %s\n",
                                pkg_name, dep_file);
                }
        }
        return NULL;
}
