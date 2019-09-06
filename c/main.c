/*
 * Copyright (C) 2018-2019 Jason Graham <jgraham@compukix.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 * @brief Main program file for sbopkg-dep2sqf
 *
 */

#include <assert.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <libbds/bds_queue.h>
#include <libbds/bds_stack.h>
#include <libbds/bds_vector.h>

#include "config.h"
#include "pkg_graph.h"
#include "pkg_ops.h"
#include "pkg_util.h"
#include "sbo.h"
#include "slack_pkg.h"
#include "user_config.h"

#define LONG_OPT(long_opt, opt)                                                                                   \
        {                                                                                                         \
                long_opt, no_argument, 0, opt                                                                     \
        }

static const char *options_str            = "acDdeghiklnpRrsUu";
static const struct option long_options[] = {
    /* These options set a flag. */
    LONG_OPT("add-reviewed", 'a'),        /* option */
    LONG_OPT("check-installed", 'c'),     /* option */
    LONG_OPT("update-pkgdb", 'D'),        /* action */
    LONG_OPT("deep", 'd'),                /* option */
    LONG_OPT("edit", 'e'),                /* action */
    LONG_OPT("graph", 'g'),               /* action */
    LONG_OPT("help", 'h'),                /* action */
    LONG_OPT("show-info", 'i'),           /* action */
    LONG_OPT("check-any-installed", 'k'), /* option */
    LONG_OPT("list", 'l'),                /* option */
    LONG_OPT("no-recursive", 'n'),        /* option */
    LONG_OPT("revdeps", 'p'),             /* option */
    LONG_OPT("review", 'R'),              /* action */
    LONG_OPT("remove", 'r'),              /* action */
    LONG_OPT("search", 's'),              /* action */
    LONG_OPT("check-updated", 'U'),       /* action */
    LONG_OPT("update", 'u'),              /* action */
    {0, 0, 0, 0}};

static const struct option *find_option(const char *long_name, const int name)
{
        assert(long_name || name);

        const struct option *opt = long_options;
        while (opt->name) {
                if (long_name) {
                        if (strcmp(opt->name, long_name) == 0) {
                                return opt;
                        }
                } else {
                        if (name == opt->val) {
                                return opt;
                        }
                }
                ++opt;
        }

        return NULL;
}

enum action {
        ACTION_NONE,
        ACTION_CHECK_UPDATES,
        ACTION_UPDATEDB,
        ACTION_REVIEW,
        ACTION_SHOW_INFO,
        ACTION_SEARCH_PKG,
        ACTION_EDIT_DEP,
        ACTION_HELP,
        ACTION_WRITE_REMOVE_SQF,
        ACTION_WRITE_SQF,
        ACTION_WRITE_UPDATE_SQF,
};

struct action_struct {
        enum action action;
        const struct option *option;
};

void set_action(struct action_struct *as, enum action value, const struct option *option)
{
        assert(option);

        if (as->action != ACTION_NONE && as->option != option) {

                fprintf(stderr, "argument --%s/-%c conflicts with argument --%s/-%c\n", as->option->name,
                        as->option->val, option->name, option->val);
                exit(EXIT_FAILURE);
        }
        as->action = value;
        as->option = option;
}

enum output_mode { OUTPUT_STDOUT = 1, OUTPUT_FILE };

static void print_help();
static int check_updates(struct pkg_graph *pkg_graph, const char *pkg_name);
static int update_pkgdb(struct pkg_graph *pkg_graph);
static int edit_pkg_dep(struct pkg_graph *pkg_graph, const char *pkg_name);
static int write_pkg_sqf(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options pkg_options,
                         enum output_mode output_mode);
static int write_pkg_update_sqf(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options pkg_options,
                                enum output_mode output_mode);
static int write_pkg_remove_sqf(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options pkg_options,
                                enum output_mode output_mode);
static int review_pkg(struct pkg_graph *pkg_graph, const char *pkg_name);
static int show_pkg_info(struct pkg_graph *pkg_graph, const char *pkg_name);
static int search_pkg(const pkg_nodes_t *sbo_pkgs, const char *pkg_name);

int main(int argc, char **argv)
{
        bool pkg_name_required       = true;
        bool pkg_name_optional       = false;
        enum output_mode output_mode = OUTPUT_FILE;
        bool create_graph            = true;

        struct pkg_graph *pkg_graph    = NULL;
        pkg_nodes_t *sbo_pkgs          = NULL;
        struct pkg_options pkg_options = pkg_options_default();

        if (setvbuf(stdout, NULL, _IONBF, 0) != 0)
                perror("setvbuf()");

        user_config_init();

        struct action_struct as = {ACTION_NONE, NULL};

        while (1) {
                int option_index = 0;
                char c           = getopt_long(argc, argv, options_str, long_options, &option_index);

                if (c == -1)
                        break;

                switch (c) {
                case 'a':
                        pkg_options.reviewed_auto_add = true;
                        break;
                case 'c':
                        pkg_options.check_installed |= PKG_CHECK_INSTALLED;
                        break;
                case 'D':
                        set_action(&as, ACTION_UPDATEDB, find_option(NULL, 'D'));
                        pkg_name_required = false;
                        break;
                case 'd':
                        pkg_options.deep = true;
                        break;
                case 'e':
                        set_action(&as, ACTION_EDIT_DEP, find_option(NULL, 'e'));
                        break;
                case 'g':
                        create_graph = true;
                        break;
                case 'h':
                        print_help();
                        exit(0);
                case 'i':
                        set_action(&as, ACTION_SHOW_INFO, find_option(NULL, 'i'));
                        break;
                case 'k':
                        pkg_options.check_installed |= PKG_CHECK_ANY_INSTALLED;
                        break;
                case 'l':
                        output_mode = OUTPUT_STDOUT;
                        break;
                case 'n':
                        pkg_options.recursive = false;
                        break;
                case 'p':
                        if (as.action == ACTION_WRITE_REMOVE_SQF) {
                                fprintf(stderr, "option --revdeps is ignored when using --remove\n");
                                break;
                        }
                        pkg_options.revdeps = true;
                        break;
                case 'R':
                        set_action(&as, ACTION_REVIEW, find_option(NULL, 'R'));
                        break;
                case 'r':
                        if (pkg_options.revdeps) {
                                fprintf(stderr, "option --revdeps is ignored when using --remove\n");
                                pkg_options.revdeps = false;
                        }
                        set_action(&as, ACTION_WRITE_REMOVE_SQF, find_option(NULL, 'r'));
                        break;
                case 's':
                        set_action(&as, ACTION_SEARCH_PKG, find_option(NULL, 's'));
                        break;
                case 'U':
                        set_action(&as, ACTION_CHECK_UPDATES, find_option(NULL, 'U'));
                        pkg_name_required = false;
                        pkg_name_optional = true;
                        break;
                case 'u':
                        set_action(&as, ACTION_WRITE_UPDATE_SQF, find_option(NULL, 'u'));
                        pkg_name_required = false;
                        pkg_name_optional = true;
                        break;
                default:
                        abort();
                }
        }

        const char *pkg_name = NULL;

        if (pkg_name_required) {
                if (argc - optind != 1) {
                        print_help();
                        exit(EXIT_FAILURE);
                }
                pkg_name = argv[optind];
        } else {
                if (pkg_name_optional) {
                        if (argc - optind == 1) {
                                pkg_name = argv[optind];
                        } else if (argc - optind > 1) {
                                print_help();
                                exit(EXIT_FAILURE);
                        }
                }
        }

        pkg_graph = pkg_graph_alloc();
        sbo_pkgs  = pkg_graph_sbo_pkgs(pkg_graph);

        if (create_graph) {
                if (!pkg_db_exists()) {
                        pkg_load_sbo(sbo_pkgs);
                        pkg_write_db(sbo_pkgs);
                        pkg_create_default_deps(sbo_pkgs);
                } else {
                        pkg_load_db(sbo_pkgs);
                }
        }

        int rc = 0;

        if (as.action == 0) {
                as.action = ACTION_WRITE_SQF;
        }

        switch (as.action) {
        case ACTION_REVIEW:
                rc = review_pkg(pkg_graph, pkg_name);
                break;
        case ACTION_SHOW_INFO:
                rc = show_pkg_info(pkg_graph, pkg_name);
                break;
        case ACTION_CHECK_UPDATES:
                rc = check_updates(pkg_graph, pkg_name);
                break;
        case ACTION_WRITE_UPDATE_SQF:
                rc = write_pkg_update_sqf(pkg_graph, pkg_name, pkg_options, output_mode);
                break;
        case ACTION_UPDATEDB:
                rc = update_pkgdb(pkg_graph);
                break;
        case ACTION_EDIT_DEP:
		rc = edit_pkg_dep(pkg_graph, pkg_name);
                break;
        case ACTION_WRITE_SQF:
                rc = write_pkg_sqf(pkg_graph, pkg_name, pkg_options, output_mode);
                break;
        case ACTION_WRITE_REMOVE_SQF:
                rc = write_pkg_remove_sqf(pkg_graph, pkg_name, pkg_options, output_mode);
                break;
        case ACTION_SEARCH_PKG:
                rc = search_pkg(sbo_pkgs, pkg_name);
                break;
        default:
                printf("action %d not handled\n", as.action);
        }

        if (pkg_graph) {
                pkg_graph_free(&pkg_graph);
        }

        user_config_destroy();

        return rc;
}

static void print_help()
{
        printf("Usage: %s [options] pkg\n"
               "Options:\n",
               PROGRAM_NAME);
}

enum updated_pkg_status {
        PKG_UPDATED,
        PKG_DOWNGRADED,
        PKG_REMOVED,
};

struct updated_pkg {
        enum updated_pkg_status status;
        const struct pkg_node *node;
        char *name;
        char *slack_pkg_version;
        char *sbo_version;
};

static void destroy_updated_pkg(struct updated_pkg *updated_pkg)
{
        if (updated_pkg->name)
                free(updated_pkg->name);
        if (updated_pkg->slack_pkg_version)
                free(updated_pkg->slack_pkg_version);
        if (updated_pkg->sbo_version)
                free(updated_pkg->sbo_version);

        memset(updated_pkg, 0, sizeof(*updated_pkg));
}

#define MIN(a, b) ((a) < (b) ? (a) : (b))


static int get_updated_pkgs(struct pkg_graph *pkg_graph, const char *pkg_name,
                             struct bds_queue *updated_pkg_queue)
{
        bool have_pkg          = false; /* Used only for single package pkg_name */
        pkg_nodes_t *pkg_nodes = NULL;

        if (pkg_name) {
                pkg_nodes             = pkg_nodes_alloc_reference();
                struct pkg_node *node = pkg_graph_search(pkg_graph, pkg_name);
                if (node == NULL)
                        goto finish;

                pkg_nodes_append(pkg_nodes, node);
        } else {
                pkg_nodes = pkg_graph_sbo_pkgs(pkg_graph);
        }

        const ssize_t num_slack_pkgs = slack_pkg_size();
        if (num_slack_pkgs < 0)
                return 1;

        for (ssize_t i = 0; i < num_slack_pkgs; ++i) {

                struct updated_pkg updated_pkg;

                const struct slack_pkg *slack_pkg = slack_pkg_get_const((size_t)i, user_config.sbo_tag);
                if (slack_pkg == NULL)
                        continue; /* tag did not match */

                const struct pkg_node *node = pkg_nodes_bsearch_const(pkg_nodes, slack_pkg->name);
                if (node == NULL) {
                        if (pkg_name == NULL) { /* the package has been removed */
                                updated_pkg.status            = PKG_REMOVED;
                                updated_pkg.node              = NULL;
                                updated_pkg.name              = bds_string_dup(slack_pkg->name);
                                updated_pkg.slack_pkg_version = bds_string_dup(slack_pkg->version);
                                updated_pkg.sbo_version       = NULL;
                                bds_queue_push(updated_pkg_queue, &updated_pkg);
                        }
                        continue;
                }

                if (pkg_name)
                        have_pkg = true;

                const char *sbo_version = sbo_read_version(node->pkg.sbo_dir, node->pkg.name);
                assert(sbo_version);

                int diff = compar_versions(slack_pkg->version, sbo_version);
                if (diff == 0)
                        continue;

                updated_pkg.status            = (diff < 0 ? PKG_UPDATED : PKG_DOWNGRADED);
                updated_pkg.node              = node;
                updated_pkg.name              = bds_string_dup(node->pkg.name);
                updated_pkg.slack_pkg_version = bds_string_dup(slack_pkg->version);
                updated_pkg.sbo_version       = bds_string_dup(sbo_version);
                bds_queue_push(updated_pkg_queue, &updated_pkg);
        }
finish:
        if (pkg_name) {
                if (!have_pkg) {
                        /* Check if it's installed */
                        const struct slack_pkg *slack_pkg = slack_pkg_search_const(pkg_name, user_config.sbo_tag);
                        if (slack_pkg) {
                                struct updated_pkg updated_pkg;
                                updated_pkg.status            = PKG_REMOVED;
                                updated_pkg.node              = NULL;
                                updated_pkg.name              = bds_string_dup(slack_pkg->name);
                                updated_pkg.slack_pkg_version = bds_string_dup(slack_pkg->version);
                                updated_pkg.sbo_version       = NULL;
                                bds_queue_push(updated_pkg_queue, &updated_pkg);
                        }
                }
                pkg_nodes_free(&pkg_nodes);
        }
        return 0;
}

static int check_updates(struct pkg_graph *pkg_graph, const char *pkg_name)
{
        int rc = 0;

        struct bds_queue *updated_pkg_queue =
            bds_queue_alloc(1, sizeof(struct updated_pkg), (void (*)(void *))destroy_updated_pkg);

        bds_queue_set_autoresize(updated_pkg_queue, true);

        if ((rc = get_updated_pkgs(pkg_graph, pkg_name, updated_pkg_queue)) != 0)
                goto finish;

        struct updated_pkg updated_pkg;

        while (bds_queue_pop(updated_pkg_queue, &updated_pkg)) {
                switch (updated_pkg.status) {
                case PKG_UPDATED:
                        printf(COLOR_OK "%4s" COLOR_END " %-24s %-8s --> %s\n", "[U]", updated_pkg.name,
                               updated_pkg.slack_pkg_version, updated_pkg.sbo_version);
                        break;
                case PKG_DOWNGRADED:
                        printf(COLOR_INFO "%4s" COLOR_END " %-24s %-8s --> %s\n", "[D]",
                               updated_pkg.name, updated_pkg.slack_pkg_version, updated_pkg.sbo_version);
                        break;
                default: /* PKG_REMOVED */
                        printf(COLOR_FAIL "%4s" COLOR_END " %-24s %-8s\n", "[R]", updated_pkg.name,
                               updated_pkg.slack_pkg_version);
                }
                destroy_updated_pkg(&updated_pkg);
        }

finish:
        if (updated_pkg_queue)
                bds_queue_free(&updated_pkg_queue);

        return rc;
}

static int update_pkgdb(struct pkg_graph *pkg_graph)
{
        int rc                    = 0;
        pkg_nodes_t *sbo_pkgs     = pkg_graph_sbo_pkgs(pkg_graph);
        pkg_nodes_t *new_sbo_pkgs = pkg_nodes_alloc();

        if ((rc = pkg_load_sbo(new_sbo_pkgs)) != 0)
                return rc;

        rc       = pkg_compar_sets(new_sbo_pkgs, sbo_pkgs);
        sbo_pkgs = pkg_graph_assign_sbo_pkgs(pkg_graph, new_sbo_pkgs);

        if (rc != 0)
                return rc;

        if ((rc = pkg_write_db(sbo_pkgs)) != 0)
                return rc;

        return pkg_create_default_deps(sbo_pkgs);
}

static int review_pkg(struct pkg_graph *pkg_graph, const char *pkg_name)
{
        int rc                     = 0;
        pkg_nodes_t *reviewed_pkgs = NULL;
        bool reviewed_pkgs_dirty   = false;

        struct pkg_node *reviewed_node  = NULL;
        const struct pkg_node *pkg_node = NULL;

        struct pkg_options pkg_options = pkg_options_default();

        pkg_options.recursive = false;

        rc = pkg_load_dep(pkg_graph, pkg_name, pkg_options);
        if (rc != 0)
                return rc;

        pkg_node = (const struct pkg_node *)pkg_graph_search(pkg_graph, pkg_name);
        if (pkg_node == NULL) {
                fprintf(stderr, "package %s does not exist\n", pkg_name);
                rc = 1;
                return rc;
        }

        reviewed_pkgs = pkg_nodes_alloc();
        if (pkg_reviewed_exists()) {
                rc = pkg_load_reviewed(reviewed_pkgs);
                if (rc != 0)
                        goto finish;
        }

        reviewed_node = pkg_nodes_bsearch(reviewed_pkgs, pkg_name);

        if (reviewed_node && reviewed_node->pkg.info_crc == pkg_node->pkg.info_crc) {
                rc = pkg_review(&pkg_node->pkg);
        } else {
                rc = pkg_review_prompt(&pkg_node->pkg);
                if (rc == 0) {
                        if (reviewed_node) {
                                if (reviewed_node->pkg.info_crc != pkg_node->pkg.info_crc) {
                                        pkg_set_version(&reviewed_node->pkg, pkg_node->pkg.version);
                                        reviewed_node->pkg.info_crc = pkg_node->pkg.info_crc;
                                        reviewed_pkgs_dirty         = true;
                                }
                        } else {
                                reviewed_node = pkg_node_alloc(pkg_node->pkg.name);
                                pkg_init_version(&reviewed_node->pkg, pkg_node->pkg.version);
                                reviewed_node->pkg.info_crc = pkg_node->pkg.info_crc;
                                pkg_nodes_insert_sort(reviewed_pkgs, reviewed_node);
                                reviewed_pkgs_dirty = true;
                        }
                        if (reviewed_pkgs_dirty)
                                rc = pkg_write_reviewed(reviewed_pkgs);
                }
        }
finish:
        if (reviewed_pkgs)
                pkg_nodes_free(&reviewed_pkgs);

        return rc;
}

static int show_pkg_info(struct pkg_graph *pkg_graph, const char *pkg_name)
{
        const struct pkg_node *pkg_node = NULL;

        pkg_node = (const struct pkg_node *)pkg_graph_search(pkg_graph, pkg_name);
        if (pkg_node == NULL) {
                fprintf(stderr, "package %s does not exist\n", pkg_name);
                return 1;
        }
        return pkg_show_info(&pkg_node->pkg);
}

static int edit_pkg_dep(struct pkg_graph *pkg_graph, const char *pkg_name)
{
        const struct pkg_node *pkg_node = NULL;

        pkg_node = (const struct pkg_node *)pkg_graph_search(pkg_graph, pkg_name);
        if (pkg_node == NULL) {
                fprintf(stderr, "package %s does not exist\n", pkg_name);
                return 1;
        }
        return edit_dep_file(pkg_node->pkg.name);
	
}

static int write_pkg_sqf(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options pkg_options,
                         enum output_mode output_mode)
{
        int rc = 0;
        char sqf_file[256];
        FILE *fp = NULL;

        pkg_nodes_t *reviewed_pkgs = NULL;
        bool reviewed_pkgs_dirty   = false;

        rc = pkg_load_dep(pkg_graph, pkg_name, pkg_options);
        if (rc != 0)
                return rc;

        if (pkg_options.revdeps) {
                rc = pkg_load_all_deps(pkg_graph, pkg_options);
                if (rc != 0)
                        return rc;
        }

        if (pkg_options.revdeps) {
                bds_string_copyf(sqf_file, sizeof(sqf_file), "%s-revdeps.sqf", pkg_name);
        } else {
                bds_string_copyf(sqf_file, sizeof(sqf_file), "%s.sqf", pkg_name);
        }

        if (output_mode == OUTPUT_FILE) {
                fp = fopen(sqf_file, "w");
                if (fp == NULL) {
                        fprintf(stderr, "unable to create %s\n", sqf_file);
                        rc = 1;
                        goto finish;
                }
        } else {
                fp = stdout;
        }

        reviewed_pkgs = pkg_nodes_alloc();
        if (pkg_reviewed_exists()) {
                rc = pkg_load_reviewed(reviewed_pkgs);
                if (rc != 0)
                        goto finish;
        }

        write_sqf(fp, pkg_graph, pkg_name, pkg_options, reviewed_pkgs, &reviewed_pkgs_dirty);

        if (reviewed_pkgs_dirty) {
                rc = pkg_write_reviewed(reviewed_pkgs);
        }

finish:
        if (reviewed_pkgs)
                pkg_nodes_free(&reviewed_pkgs);

        if (fp && fp != stdout)
                fclose(fp);

        return rc;
}

static int write_pkg_update_sqf(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options pkg_options,
                                enum output_mode output_mode)
{
        return 0;
}

static int write_pkg_remove_sqf(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options pkg_options,
                                enum output_mode output_mode)
{
        int rc = 0;
        char sqf_file[256];
        FILE *fp                       = NULL;
        const char *term               = NULL;
        struct bds_queue *pkg_list     = NULL;
        struct bds_stack *removal_list = NULL;
        struct pkg_node *node          = NULL;
        struct pkg_iterator iter;
        struct pkg_options options = pkg_options_default();
        pkg_iterator_flags_t flags = 0;
        int max_dist               = 0;

        options.revdeps = true;

        if ((rc = pkg_load_all_deps(pkg_graph, options)) != 0)
                return rc;
        if ((rc = pkg_load_dep(pkg_graph, pkg_name, options)) != 0)
                return rc;

        pkg_list = bds_queue_alloc(1, sizeof(struct pkg_node *), NULL);
        bds_queue_set_autoresize(pkg_list, true);

        removal_list = bds_stack_alloc(1, sizeof(struct pkg_node *), NULL);

        flags    = PKG_ITER_DEPS | PKG_ITER_FORW;
        max_dist = (pkg_options.deep ? -1 : 0);
        for (node = pkg_iterator_begin(&iter, pkg_graph, pkg_name, flags, max_dist); node != NULL;
             node = pkg_iterator_next(&iter)) {

                if (node->pkg.dep.is_meta)
                        continue;

                node->pkg.for_removal = true;
                bds_queue_push(pkg_list, &node);
        }
        pkg_iterator_destroy(&iter);

        while (bds_queue_pop(pkg_list, &node)) {

                flags    = PKG_ITER_REVDEPS;
                max_dist = 1;

                for (struct pkg_node *parent_node =
                         pkg_iterator_begin(&iter, pkg_graph, node->pkg.name, flags, max_dist);
                     parent_node != NULL; parent_node = pkg_iterator_next(&iter)) {

                        if (strcmp(parent_node->pkg.name, node->pkg.name) == 0)
                                continue;

                        if (parent_node->pkg.dep.is_meta)
                                continue;

                        if (!parent_node->pkg.for_removal &&
                            slack_pkg_is_installed(parent_node->pkg.name, user_config.sbo_tag)) {
                                printf(COLOR_FAIL "%12s" COLOR_END " %-24s <-- %s\n", "[required]", node->pkg.name,
                                       parent_node->pkg.name);
                                node->pkg.for_removal = false;
                                break;
                        }
                }
                pkg_iterator_destroy(&iter);

                if (node->pkg.for_removal)
                        bds_stack_push(removal_list, &node);
        }

        if (bds_stack_size(removal_list) == 0)
                goto finish;

        bds_string_copyf(sqf_file, sizeof(sqf_file), "%s-remove.sqf", pkg_name);

        if (output_mode == OUTPUT_FILE) {
                fp = fopen(sqf_file, "w");
                if (fp == NULL) {
                        fprintf(stderr, "unable to create %s\n", sqf_file);
                        rc = 1;
                        goto finish;
                }
                term = "\n";
        } else {
                fp   = stdout;
                term = " ";
        }

        while (bds_stack_pop(removal_list, &node)) {
                fprintf(fp, "%s%s", node->pkg.name, term);
        }

        if (fp != stdout) {
                printf("created %s\n", sqf_file);
        } else {
                fprintf(fp, "\n");
        }

finish:
        if (fp && fp != stdout)
                fclose(fp);

        if (pkg_list)
                bds_queue_free(&pkg_list);
        if (removal_list)
                bds_stack_free(&removal_list);

        return rc;
}

static int compar_string_ptr(const void *a, const void *b) { return strcmp(*(const char **)a, *(const char **)b); }
static int search_pkg(const pkg_nodes_t *sbo_pkgs, const char *pkg_name)
{
        size_t num_nodes            = 0;
        struct bds_vector *results  = NULL;
        size_t num_results          = 0;
        char *__pkg_name            = NULL;
        const size_t sbo_dir_offset = strlen(user_config.sbopkg_repo) + 1;

        __pkg_name = bds_string_dup(pkg_name);
        bds_string_tolower(__pkg_name);

        results   = bds_vector_alloc(1, sizeof(const char *), free);
        num_nodes = pkg_nodes_size(sbo_pkgs);
        for (size_t i = 0; i < num_nodes; ++i) {
                const struct pkg_node *node = pkg_nodes_get_const(sbo_pkgs, i);
                char *sbo_dir               = bds_string_dup(node->pkg.sbo_dir + sbo_dir_offset);
                char *p                     = bds_string_dup(node->pkg.name);
                if (bds_string_contains(bds_string_tolower(p), __pkg_name)) {
                        bds_vector_append(results, &sbo_dir);
                }
                free(p);
        }
        free(__pkg_name);

        bds_vector_qsort(results, compar_string_ptr);

        num_results = bds_vector_size(results);
        for (size_t i = 0; i < num_results; ++i) {
                printf("%s\n", *(const char **)bds_vector_get(results, i));
        }

        return 0;
}
