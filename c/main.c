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

#include <libbds/bds_string.h>
#include <libbds/bds_vector.h>

#include "config.h"
#include "deps.h"
#include "filesystem.h"
#include "input.h"
#include "msg_string.h"
#include "pkg_db.h"
#include "user_config.h"

#define LONG_OPT(long_opt, opt)                                                                                   \
        {                                                                                                         \
                long_opt, no_argument, 0, opt                                                                     \
        }

static const char *options_str            = "ckunpRsehlrg";
static const struct option long_options[] = {
    /* These options set a flag. */
    LONG_OPT("check-installed", 'c'),   
    LONG_OPT("check-foreign-installed", 'k'),    
    LONG_OPT("update-pkgdb", 'u'), /* action */
    LONG_OPT("no-recursive", 'n'),
    LONG_OPT("revdeps", 'p'), 
    LONG_OPT("review", 'R'),       /* action */
    LONG_OPT("search", 's'),       /* action */
    LONG_OPT("edit", 'e'),         /* action */
    LONG_OPT("help", 'h'),         /* action */
    LONG_OPT("list", 'l'),   
    LONG_OPT("remove", 'r'),       /* action */
    LONG_OPT("graph", 'g'), 
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
        ACTION_REVIEW,
        ACTION_ADD,
        ACTION_UPDATEDB,
        ACTION_EDIT_DEP,
	ACTION_WRITE_SQF,
	ACTION_WRITE_REMOVE_SQF,
	ACTION_SEARCH,
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

enum output_mode {
	OUTPUT_STDOUT=1,
	OUTPUT_FILE
};

int main(int argc, char **argv)
{
        bool pkg_name_required = true;
	enum output_mode output_mode = OUTPUT_FILE;
	bool create_graph = false;

	struct pkg_graph *pkg_graph = NULL;
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
                case 'c':
                        pkg_options.check_installed |= CHECK_INSTALLED;
                        break;
                case 'k':
                        pkg_options.check_installed |= CHECK_ANY_INSTALLED;
                        break;
                case 'u':
                        set_action(&as, ACTION_UPDATEDB, find_option(NULL, 'u'));
                        pkg_name_required = false;
                        break;
                case 'n':
                        process_options.recursive = false;
                        break;
                case 'p':
			if( as.action == ACTION_WRITE_REMOVE_SQF ) {
				fprintf(stderr, "option --revdeps is ignored when using --remove\n");
				break;
			}
                        pkg_options.revdeps = true;
                        break;
                case 'R':
                        set_action(&as, ACTION_REVIEW, find_option(NULL, 'R'));
                        break;
                case 's':
                        set_action(&as, ACTION_SEARCH, find_option(NULL, 's'));
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
			if( pkg_options.revdeps ) {
				fprintf(stderr, "option --revdeps is ignored when using --remove\n");
				process_options.revdeps = false;
			}
                        set_action(&as, ACTION_WRITE_REMOVE_SQF, find_option(NULL, 'r'));
                        break;
                case 's':
                        set_action(&as, ACTION_SEARCH, find_option(NULL, 's'));
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

        if (!pkg_db_exists()) {
                pkg_load_sbo(sbo_pkgs);
                pkg_write_db(sbo_pkgs);
                pkg_create_default_deps(sbo_pkgs);
        } else {
                pkg_load_db(sbo_pkgs);
        }

        int rc = 0;

	if( as.action == 0 ) {
		as.action = ACTION_WRITE_SQF;
	}

	struct pkg_node *pkg_node;
        switch (as.action) {
        case ACTION_REVIEW:
		rc = pkg_load_dep(pkg_graph, pkg_name, pkg_options);
		if( rc != 0 )
			break;
		
		pkg_node = pkg_graph_search(pkg_graph, pkg_name);
		if( pkg_node == NULL) {
			fprintf(stderr, "package %s does not exist\n", pkg_name);
			rc = 1;
			break;
		}		
		rc = pkg_review(&pkg_node->pkg);
                break;
        case ACTION_UPDATEDB:
                if( (rc = pkg_load_sbo(sbo_pkgs)) != 0 )
			break;
		if( (rc = pkg_write_db(sbo_pkgs)) != 0 )
			break;
		rc = pkg_create_default_deps(sbo_pkgs);
		break;
        case ACTION_EDIT_DEP:
		fprintf(stderr, "option --edit is not yet implementated\n");
		rc = 1;
                break;
	case ACTION_WRITE_SQF: {
		rc = pkg_load_dep(pkg_graph, pkg_name, pkg_options);
		if( rc != 0 )
			break;
		
		char sqf[256];
		if( pkg_options.revdeps ) {
			bds_string_copyf(sqf, sizeof(sqf), "%s-revdeps.sqf", pkg_name);
		} else {
			bds_string_copyf(sqf, sizeof(sqf), "%s.sqf", pkg_name);
		}
		
		FILE *fp = fopen(sqf, "w");
		if( fp == NULL ) {
			fprintf(stderr, "unable to create sqf file %s\n", sqf);
			rc = 1;
			break;
		}
		write_sqf(fp, pkg_graph, pkg_name, pkg_options);
		fclose(fp);
		break;
	}		
	case ACTION_WRITE_REMOVE_PKG: {
		/*
		  In case its a meta file, we go ahead and load it into the graph
		 */
		if( (rc = pkg_load_dep(pkg_graph, pkg_name, pkg_options)) != 0 )
			break;
		if( (rc = pkg_load_all_deps(pkg_graph, pkg_options)) != 0 )
			break;

		struct dep_list *dep_list = NULL;		
		if( process_options.revdeps ) {
			assert( as.action != ACTION_REMOVE_PKG );
			dep_list = (struct dep_list *)load_dep_parents(pkg_name, process_options, false);
		} else {
			dep_list = load_dep_list(pkg_name, process_options);
		}
		
                if (dep_list == NULL) {
                        rc = 1;
			break;
                }

		if( output_mode == OUTPUT_STDOUT ) {
			if( as.action == ACTION_WRITE_SQF ) {
				write_dep_list(stdout, dep_list);
				break;
			} else {
				write_remove_list(stdout, dep_list, process_options);
				break;
			}
		}

		char sqf[256];
		if( as.action == ACTION_REMOVE_PKG ) {
			bds_string_copyf(sqf, sizeof(sqf), "%s-remove.sqf", pkg_name);
		} else {
			if( process_options.revdeps ) {
				bds_string_copyf(sqf, sizeof(sqf), "%s-revdeps.sqf", pkg_name);
			} else {
				bds_string_copyf(sqf, sizeof(sqf), "%s.sqf", pkg_name);
			}
		}
		FILE *fp = fopen(sqf, "w");
		if( fp == NULL ) {
			fprintf(stderr, "unable to create sqf file %s\n", sqf);
			rc = 1;
			break;
		}
		if( as.action == ACTION_REMOVE_PKG ) {
			write_remove_sqf(fp, dep_list, process_options);
		} else {
			write_sqf(fp, dep_list, process_options);
		}
		fclose(fp);
		
		rc = 0;
	}
		break;
	case ACTION_SEARCH_PKG: {
		struct bds_vector *pkg_list;
		
		rc = search_sbo_repo(user_config.sbopkg_repo, pkg_name, &pkg_list);
		if( rc == 0 && pkg_list ) {
			for( size_t i=0; i < bds_vector_size(pkg_list); ++i ) {
				printf("%s\n", *(char **)bds_vector_get(pkg_list, i));
			}
		}
		if( pkg_list )
			bds_vector_free(&pkg_list);
	}
		break;
	default:
		printf("action %d not handled\n", as.action);
        }

        destroy_user_config();
        fini_pkg_db();

        return rc;
}

#define PROGRAM_NAME "sbopkg-dep2sqf"
static void print_help()
{
        printf("Usage: %s [options] pkg\n"
               "Options:\n",
               PROGRAM_NAME);
}
