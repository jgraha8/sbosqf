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
#include "user_config.h"

#define LONG_OPT(long_opt, opt)                                                                                   \
        {                                                                                                         \
                long_opt, no_argument, 0, opt                                                                     \
        }

static const char *options_str            = "ckunpRsehlrg";
static const struct option long_options[] = {
    /* These options set a flag. */
    LONG_OPT("check-installed", 'c'),         /* option */
    LONG_OPT("check-foreign-installed", 'k'), /* option */
    LONG_OPT("update-pkgdb", 'u'),            /* action */
    LONG_OPT("no-recursive", 'n'),            /* option */
    LONG_OPT("revdeps", 'p'),                 /* option */
    LONG_OPT("review", 'R'),                  /* action */
    LONG_OPT("search", 's'),                  /* action */
    LONG_OPT("edit", 'e'),                    /* action */
    LONG_OPT("help", 'h'),                    /* action */
    LONG_OPT("list", 'l'),                    /* option */
    LONG_OPT("remove", 'r'),                  /* action */
    LONG_OPT("graph", 'g'),                   /* action */
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

static void print_help();

enum output_mode { OUTPUT_STDOUT = 1, OUTPUT_FILE };

int main(int argc, char **argv)
{
        bool pkg_name_required       = true;
        enum output_mode output_mode = OUTPUT_FILE;
        bool create_graph            = true;

        struct pkg_graph *pkg_graph    = NULL;
        pkg_nodes_t *sbo_pkgs          = NULL;
        struct pkg_options pkg_options = pkg_options_default();

	pkg_options.reviewed_auto_add = true;

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
                case 'c':
                        pkg_options.check_installed |= PKG_CHECK_INSTALLED;
                        break;
                case 'k':
                        pkg_options.check_installed |= PKG_CHECK_ANY_INSTALLED;
                        break;
                case 'u':
                        set_action(&as, ACTION_UPDATEDB, find_option(NULL, 'u'));
                        pkg_name_required = false;
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
                case 's':
                        set_action(&as, ACTION_SEARCH_PKG, find_option(NULL, 's'));
                        break;
                case 'e':
                        set_action(&as, ACTION_EDIT_DEP, find_option(NULL, 'e'));
                        break;
                case 'h':
                        print_help();
                        exit(0);
                case 'l':
                        output_mode = OUTPUT_STDOUT;
                        break;
                case 'g':
                        create_graph = true;
                        break;
                case 'r':
                        if (pkg_options.revdeps) {
                                fprintf(stderr, "option --revdeps is ignored when using --remove\n");
                                pkg_options.revdeps = false;
                        }
                        set_action(&as, ACTION_WRITE_REMOVE_SQF, find_option(NULL, 'r'));
                        break;
                default:
                        abort();
                }
        }

        struct pkg_node *pkg_node;	
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

	if( create_graph ) {
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
                pkg_options.recursive = false;
                rc                    = pkg_load_dep(pkg_graph, pkg_name, pkg_options);
                if (rc != 0)
                        break;

                pkg_node = pkg_graph_search(pkg_graph, pkg_name);
                if (pkg_node == NULL) {
                        fprintf(stderr, "package %s does not exist\n", pkg_name);
                        rc = 1;
                        break;
                }

                pkg_nodes_t *reviewed_pkgs = pkg_nodes_alloc();
                bool reviewed_pkgs_dirty   = false;

                if (pkg_reviewed_exists())
                        rc = pkg_load_reviewed(reviewed_pkgs);

                rc = pkg_review_prompt(&pkg_node->pkg);
                if (rc == 0) {
                        struct pkg_node *reviewed_node = pkg_nodes_bsearch(reviewed_pkgs, pkg_node->pkg.name);
                        if (reviewed_node) {
                                if (reviewed_node->pkg.info_crc != pkg_node->pkg.info_crc) {
                                        reviewed_node->pkg.info_crc = pkg_node->pkg.info_crc;
                                        reviewed_pkgs_dirty         = true;
                                }
                        } else {
                                // Create new node
                                reviewed_node               = pkg_node_alloc(pkg_node->pkg.name);
                                reviewed_node->pkg.info_crc = pkg_node->pkg.info_crc;
                                pkg_nodes_insert_sort(reviewed_pkgs, reviewed_node);
                                reviewed_pkgs_dirty = true;
                        }
                        if (reviewed_pkgs_dirty)
                                rc = pkg_write_reviewed(reviewed_pkgs);
                }
                pkg_nodes_free(&reviewed_pkgs);

                break;
        case ACTION_UPDATEDB: {
                pkg_nodes_t *new_sbo_pkgs  = pkg_nodes_alloc();

                if ((rc = pkg_load_sbo(new_sbo_pkgs)) != 0)
                        break;

                rc = pkg_compar_sets(new_sbo_pkgs, sbo_pkgs);
		sbo_pkgs = pkg_graph_assign_sbo_pkgs(pkg_graph, new_sbo_pkgs);
		
                if (rc != 0)
                        break;

                if ((rc = pkg_write_db(sbo_pkgs)) != 0)
                        break;

                rc = pkg_create_default_deps(sbo_pkgs);
        } break;
        case ACTION_EDIT_DEP:
                fprintf(stderr, "option --edit is not yet implementated\n");
                rc = 1;
                break;
        case ACTION_WRITE_SQF: {
                rc = pkg_load_dep(pkg_graph, pkg_name, pkg_options);
                if (rc != 0)
                        break;
                if (pkg_options.revdeps) {
                        rc = pkg_load_all_deps(pkg_graph, pkg_options);
                        if (rc != 0)
                                break;
                }

                char sqf[256];
                if (pkg_options.revdeps) {
                        bds_string_copyf(sqf, sizeof(sqf), "%s-revdeps.sqf", pkg_name);
                } else {
                        bds_string_copyf(sqf, sizeof(sqf), "%s.sqf", pkg_name);
                }

                FILE *fp = NULL;
                if (output_mode == OUTPUT_FILE) {
                        fp = fopen(sqf, "w");
                        if (fp == NULL) {
                                fprintf(stderr, "unable to create sqf file %s\n", sqf);
                                rc = 1;
                                break;
                        }
                } else {
                        fp = stdout;
                }

                pkg_nodes_t *reviewed_pkgs = pkg_nodes_alloc();
                bool reviewed_pkgs_dirty   = false;

                if (pkg_reviewed_exists())
                        rc = pkg_load_reviewed(reviewed_pkgs);

                if (rc == 0) {
                        write_sqf(fp, pkg_graph, pkg_name, pkg_options, reviewed_pkgs, &reviewed_pkgs_dirty);
                }

                if (reviewed_pkgs_dirty) {
                        rc = pkg_write_reviewed(reviewed_pkgs);
                }
                pkg_nodes_free(&reviewed_pkgs);

                if (fp != stdout)
                        fclose(fp);
                break;
        }
#if 0
        case ACTION_WRITE_REMOVE_PKG: {
                /*
                  In case its a meta file, we go ahead and load it into the graph
                 */
                if ((rc = pkg_load_dep(pkg_graph, pkg_name, pkg_options)) != 0)
                        break;
                if ((rc = pkg_load_all_deps(pkg_graph, pkg_options)) != 0)
                        break;

                struct dep_list *dep_list = NULL;
                if (pkg_options.revdeps) {
                        assert(as.action != ACTION_REMOVE_PKG);
                        dep_list = (struct dep_list *)load_dep_parents(pkg_name, pkg_options, false);
                } else {
                        dep_list = load_dep_list(pkg_name, pkg_options);
                }

                if (dep_list == NULL) {
                        rc = 1;
                        break;
                }

                if (output_mode == OUTPUT_STDOUT) {
                        if (as.action == ACTION_WRITE_SQF) {
                                write_dep_list(stdout, dep_list);
                                break;
                        } else {
                                write_remove_list(stdout, dep_list, pkg_options);
                                break;
                        }
                }

                char sqf[256];
                if (as.action == ACTION_REMOVE_PKG) {
                        bds_string_copyf(sqf, sizeof(sqf), "%s-remove.sqf", pkg_name);
                } else {
                        if (pkg_options.revdeps) {
                                bds_string_copyf(sqf, sizeof(sqf), "%s-revdeps.sqf", pkg_name);
                        } else {
                                bds_string_copyf(sqf, sizeof(sqf), "%s.sqf", pkg_name);
                        }
                }
                FILE *fp = fopen(sqf, "w");
                if (fp == NULL) {
                        fprintf(stderr, "unable to create sqf file %s\n", sqf);
                        rc = 1;
                        break;
                }
                if (as.action == ACTION_REMOVE_PKG) {
                        write_remove_sqf(fp, dep_list, pkg_options);
                } else {
                        write_sqf(fp, dep_list, pkg_options);
                }
                fclose(fp);

                rc = 0;
        } break;
#endif
        case ACTION_SEARCH_PKG: {
                if ((rc = pkg_load_db(sbo_pkgs)) != 0)
                        break;

                char *__pkg_name = bds_string_dup(pkg_name);

                bds_string_tolower(__pkg_name);

                size_t num_nodes = pkg_nodes_size(sbo_pkgs);
                for (size_t i = 0; i < num_nodes; ++i) {
                        char p[64];

                        const struct pkg_node *node = pkg_nodes_get_const(sbo_pkgs, i);
                        bds_string_copyf(p, sizeof(p), "%s", node->pkg.name);

                        if (bds_string_contains(bds_string_tolower(p), __pkg_name)) {
                                printf("%s\n", node->pkg.sbo_dir);
                        }
                }
                free(__pkg_name);
        } break;
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
