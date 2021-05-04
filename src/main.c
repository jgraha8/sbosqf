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
#include <stdio.h>

#include <libbds/bds_queue.h>
#include <libbds/bds_stack.h>
#include <libbds/bds_vector.h>

#include "build.h"
#include "check_updates.h"
#include "config.h"
#include "edit.h"
#include "info.h"
#include "make_meta.h"
#include "mesg.h"
#include "pkg_io.h"
#include "remove.h"
#include "review.h"
#include "search.h"
#include "update.h"
#include "updatedb.h"
#include "user_config.h"
#include "utils.h"
#include "pkg_ops.h"
#include "pkg_util.h"

static bool pkg_name_required  = true;
static bool pkg_name_optional  = false;
static bool multiple_pkg_names = false;
static bool create_graph       = true;
static bool dep_file_required  = true;
// static enum slack_pkg_dbi_type pkg_dbi_type       = SLACK_PKG_DBI_PACKAGES;

enum command {
        COMMAND_NONE,
        COMMAND_CHECK_UPDATES,
        COMMAND_UPDATEDB,
        COMMAND_REVIEW,
        COMMAND_INFO,
        COMMAND_SEARCH,
        COMMAND_EDIT,
        COMMAND_HELP,
        COMMAND_REMOVE,
        COMMAND_BUILD,
        COMMAND_UPDATE,
        COMMAND_MAKE_META
};

struct command_struct {
        enum command         command;
        const struct option *option;
};

static void print_help();

int main(int argc, char **argv)
{
        int rc = 0;

        struct pkg_graph *   pkg_graph   = NULL;
        pkg_nodes_t *        sbo_pkgs    = NULL;
        struct pkg_options   pkg_options = pkg_options_default();
        string_list_t *      pkg_names   = NULL;
        struct slack_pkg_dbi slack_pkg_dbi;

        if (setvbuf(stdout, NULL, _IONBF, 0) != 0)
                perror("setvbuf()");

        user_config_init();

        struct command_struct cs = {COMMAND_NONE, NULL};

        /*
          Get command
         */
        if (argc < 2) {
                mesg_error("no command provided\n");
                exit(EXIT_FAILURE);
        }

        {
                int num_opts = 0;

                const char *cmd = argv[1];
                if (strcmp(cmd, "build") == 0) {
                        cs.command         = COMMAND_BUILD;
                        num_opts           = process_build_options(argc, argv, &pkg_options);
                        multiple_pkg_names = true;

                } else if (strcmp(cmd, "remove") == 0) {
                        cs.command         = COMMAND_REMOVE;
                        num_opts           = process_remove_options(argc, argv, &pkg_options);
                        multiple_pkg_names = true;

                } else if (strcmp(cmd, "update") == 0) {
                        cs.command         = COMMAND_UPDATE;
                        num_opts           = process_update_options(argc, argv, &pkg_options);
                        multiple_pkg_names = true;

                } else if (strcmp(cmd, "edit") == 0) {
                        cs.command = COMMAND_EDIT;
                        num_opts   = process_edit_options(argc, argv, &pkg_options);
                } else if (strcmp(cmd, "help") == 0) {
                        print_help();
                        exit(EXIT_SUCCESS);
                } else if (strcmp(cmd, "info") == 0) {
                        cs.command = COMMAND_INFO;
                        num_opts   = process_info_options(argc, argv, &pkg_options);
                } else if (strcmp(cmd, "review") == 0) {
                        cs.command = COMMAND_REVIEW;
                        num_opts   = process_review_options(argc, argv, &pkg_options);
                } else if (strcmp(cmd, "search") == 0) {
                        cs.command = COMMAND_SEARCH;
                        num_opts   = process_search_options(argc, argv, &pkg_options);
                } else if (strcmp(cmd, "updatedb") == 0) {
                        cs.command        = COMMAND_UPDATEDB;
                        pkg_name_required = false;
                        num_opts          = process_updatedb_options(argc, argv, &pkg_options);
                } else if (strcmp(cmd, "check-updates") == 0) {
                        cs.command        = COMMAND_CHECK_UPDATES;
                        pkg_name_required = false;
                        pkg_name_optional = true;
                        num_opts          = process_check_updates_options(argc, argv, &pkg_options);
                } else if (strcmp(cmd, "make-meta") == 0) {
                        cs.command         = COMMAND_MAKE_META;
                        num_opts           = process_make_meta_options(argc, argv, &pkg_options);
                        multiple_pkg_names = true;
                } else {
                        mesg_error("incorrect command provided: %s\n", cmd);
                        exit(EXIT_FAILURE);
                }

                if (num_opts < 0) {
                        mesg_error("unable to process command %s\n", cmd);
                        exit(EXIT_FAILURE);
                }

                argc -= MAX(num_opts + 1, 1);
                argv += MAX(num_opts + 1, 1);
        }

        slack_pkg_dbi = slack_pkg_dbi_create(pkg_options.pkg_dbi_type);
        pkg_graph     = pkg_graph_alloc();
        sbo_pkgs      = pkg_graph_sbo_pkgs(pkg_graph);

        if (create_graph) {
                if (!pkg_db_exists()) {
                        pkg_load_sbo(sbo_pkgs);
                        if ((rc = pkg_write_db(sbo_pkgs)) != 0) {
                                return 1;
                        }
                        pkg_create_default_deps(sbo_pkgs);
                } else {
                        pkg_load_db(sbo_pkgs);
                }
        }

        const char *pkg_name = NULL;
        if (pkg_name_required) {
                if (argc == 0) {
                        mesg_error("no package names provided\n");
                        exit(EXIT_FAILURE);
                }
                if (multiple_pkg_names) {
                        pkg_names = string_list_alloc_reference();
                        for (int i = 0; i < argc; ++i) {
                                if (dep_file_required) {
                                        if (!pkg_dep_file_exists(argv[i])) {
                                                mesg_error("dependency file %s does not exist\n", argv[i]);
                                                exit(EXIT_FAILURE);
                                        }
                                }
                                string_list_append(pkg_names, argv[i]);
                        }
                } else {
                        if (argc != 1) {
                                print_help();
                                exit(EXIT_FAILURE);
                        }
                        pkg_name = argv[0];
                }
        } else {
                if (pkg_name_optional) {
                        if (argc == 1) {
                                pkg_name = argv[0];
                        } else if (argc > 1) {
                                print_help();
                                exit(EXIT_FAILURE);
                        }
                }
        }

        if (cs.command == 0) {
                cs.command = COMMAND_BUILD;
        }

        switch (cs.command) {
        case COMMAND_REVIEW:
                rc = run_review_command(pkg_graph, pkg_name);
                if (rc < 0) {
                        mesg_error("unable to review package %s\n", pkg_name);
                } else if (rc > 0) {
                        mesg_warn("package %s not added to REVIEWED\n", pkg_name);
                }
                break;
        case COMMAND_INFO:
                rc = run_info_command(pkg_graph, pkg_name);
                if (rc != 0) {
                        mesg_error("unable to show package %s\n", pkg_name);
                }
                break;
        case COMMAND_CHECK_UPDATES:
                rc = run_check_updates_command(&slack_pkg_dbi, pkg_graph, pkg_name);
                if (rc != 0) {
                        if (pkg_name) {
                                mesg_error("unable to check updates for package %s\n", pkg_name);
                        } else {
                                mesg_error("unable to check updates\n");
                        }
                }
                break;
        case COMMAND_UPDATE:
                rc = run_update_command(&slack_pkg_dbi, pkg_graph, pkg_names, pkg_options);
                if (rc != 0) {
                        mesg_error("unable to create update package list\n");
                }
                break;
        case COMMAND_UPDATEDB:
                rc = run_updatedb_command(pkg_graph);
                if (rc != 0) {
                        mesg_error("unable to update package database\n");
                }
                break;
        case COMMAND_EDIT:
                rc = run_edit_command(pkg_graph, pkg_name);
                if (rc != 0) {
                        mesg_error("unable to edit package dependency file %s\n", pkg_name);
                }
                break;
        case COMMAND_BUILD:
                rc = run_build_command(&slack_pkg_dbi, pkg_graph, pkg_names, pkg_options);
                if (rc != 0) {
                        mesg_error("unable to create dependency list for package %s\n", pkg_name);
                }
                break;
        case COMMAND_REMOVE:
                rc = run_remove_command(&slack_pkg_dbi, pkg_graph, pkg_names, pkg_options);
                if (rc != 0) {
                        mesg_error("unable to create remove package list\n");
                }
                break;
        case COMMAND_SEARCH:
                rc = run_search_command(pkg_graph, pkg_name);
                if (rc != 0) {
                        mesg_error("unable to search for package %s\n", pkg_name);
                }
                break;
        case COMMAND_MAKE_META:
                rc = run_make_meta_command(sbo_pkgs, pkg_options.output_name, pkg_names);
                if (rc != 0) {
                        mesg_error("unable to make meta-package %s\n", pkg_options.output_name);
                } else {
                        mesg_ok("created meta-package %s\n", pkg_options.output_name);
                }
                break;
        default:
                mesg_warn("command %d not handled\n", cs.command);
        }

        if (pkg_names) {
                string_list_free(&pkg_names);
        }
        if (pkg_graph) {
                pkg_graph_free(&pkg_graph);
        }

        user_config_destroy();

        return rc;
}

static void print_help()
{
        printf("Usage: %s command [options] [args]\n"
               "Commands:\n"
               "  create [options] pkg       create package dependency list or file\n"
               "  updatedb                   update the package database\n"
               "  check-updates [pkg]        check for updates\n"
               "  help                       show this information\n"
               "  review pkg                 review package and depenency file information\n"
               "  edit pkg                   edit package dependency file\n",
               PROGRAM_NAME);
}
