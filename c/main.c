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
#include "pkg.h"
#include "pkg_util.h"
#include "sbo.h"
#include "slack_pkg.h"
#include "user_config.h"

#define LONG_OPT(long_opt, opt)                                                                                   \
        {                                                                                                         \
                long_opt, no_argument, 0, opt                                                                     \
        }

static const char *options_str            = "acdeghklnpRrsUu";
static const struct option long_options[] = {
    /* These options set a flag. */
    LONG_OPT("add-reviewed", 'a'),            /* option */
    LONG_OPT("check-installed", 'c'),         /* option */
    LONG_OPT("deep", 'd'),                    /* option */
    LONG_OPT("edit", 'e'),                    /* action */
    LONG_OPT("graph", 'g'),                   /* action */
    LONG_OPT("help", 'h'),                    /* action */
    LONG_OPT("check-foreign-installed", 'k'), /* option */
    LONG_OPT("list", 'l'),                    /* option */
    LONG_OPT("no-recursive", 'n'),            /* option */
    LONG_OPT("revdeps", 'p'),                 /* option */
    LONG_OPT("review", 'R'),                  /* action */
    LONG_OPT("remove", 'r'),                  /* action */
    LONG_OPT("search", 's'),                  /* action */
    LONG_OPT("check-updated", 'U'),           /* action */
    LONG_OPT("update-pkgdb", 'u'),            /* action */
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
        ACTION_SEARCH_PKG,
        ACTION_EDIT_DEP,
        ACTION_HELP,
        ACTION_WRITE_REMOVE_SQF,
        ACTION_WRITE_SQF,
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
static int check_updates(struct pkg_graph *pkg_graph);
static int update_pkgdb(struct pkg_graph *pkg_graph);
static int write_pkg_sqf(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options pkg_options,
                         enum output_mode output_mode);
static int write_pkg_remove_sqf(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options pkg_options,
                                enum output_mode output_mode);
static int review_pkg(struct pkg_graph *pkg_graph, const char *pkg_name);
static int search_pkg(const pkg_nodes_t *sbo_pkgs, const char *pkg_name);

int main(int argc, char **argv)
{
        bool pkg_name_required       = true;
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
                        break;
                case 'u':
                        set_action(&as, ACTION_UPDATEDB, find_option(NULL, 'u'));
                        pkg_name_required = false;
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
        case ACTION_CHECK_UPDATES:
                rc = check_updates(pkg_graph);
                break;
        case ACTION_UPDATEDB:
                rc = update_pkgdb(pkg_graph);
                break;
        case ACTION_EDIT_DEP:
                fprintf(stderr, "option --edit is not yet implementated\n");
                rc = 1;
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

#define PROGRAM_NAME "sbopkg-dep2sqf"
static void print_help()
{
        printf("Usage: %s [options] pkg\n"
               "Options:\n",
               PROGRAM_NAME);
}


static int check_updates(struct pkg_graph *pkg_graph)
{
        const pkg_nodes_t *sbo_pkgs = pkg_graph_sbo_pkgs(pkg_graph);
        const size_t num_nodes      = pkg_nodes_size(sbo_pkgs);

        for (size_t i = 0; i < num_nodes; ++i) {
                const struct pkg_node *node = pkg_nodes_get_const(sbo_pkgs, i);

                const struct slack_pkg *slack_pkg = slack_pkg_search_const(node->pkg.name, user_config.sbo_tag);
                if (slack_pkg) {
                        const char *sbo_version = sbo_read_version(node->pkg.sbo_dir, node->pkg.name);
                        assert(sbo_version);

                        int diff = strcmp(slack_pkg->version, sbo_version);
                        if (diff == 0)
                                continue;
                        if (diff < 0) {
                                printf(COLOR_OK "  [U]" COLOR_END " %-24s %-8s --> %s\n", node->pkg.name, slack_pkg->version,
                                       sbo_version);
                        } else {
                                printf(COLOR_WARN "  [D]" COLOR_END " %-24s %-8s --> %s\n", node->pkg.name, slack_pkg->version,
                                       sbo_version);
                        }
                }
        }

	return 0;
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
                                        reviewed_node->pkg.info_crc = pkg_node->pkg.info_crc;
                                        reviewed_pkgs_dirty         = true;
                                }
                        } else {
                                reviewed_node               = pkg_node_alloc(pkg_node->pkg.name);
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
                                printf("  %s required by at least %s\n", node->pkg.name, parent_node->pkg.name);
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
