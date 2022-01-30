#define _DEFAULT_SOURCE
#include <stdio.h>
#include <dirent.h>
#include <string.h>

#include "options.h"
#include "pkg.h"
#include "pkg_graph.h"
#include "pkg_io.h"
#include "pkg_util.h"
#include "mesg.h"
#include "user_config.h"

void print_search_help()
{
        printf("Usage: %s search [option] pkg\n"
               "Options:\n"
               "  -h, --help\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

int process_search_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *        options_str    = "h";
        static const struct option long_options[] = {LONG_OPT("help", 'h'), {0, 0, 0, 0}};

        return process_options(argc, argv, options_str, long_options, print_search_help, options);
}

static int find_all_meta_pkgs(pkg_nodes_t *meta_pkgs)
{
        struct dirent *dirent = NULL;

        DIR *dp = opendir(user_config.depdir);
        if (dp == NULL)
                return 1;

        while ((dirent = readdir(dp)) != NULL) {
                if (dirent->d_type == DT_DIR)
                        continue;

                if (NULL == pkg_nodes_bsearch_const(meta_pkgs, dirent->d_name)) {
                        if (pkg_is_meta(dirent->d_name)) {
                                struct pkg_node *meta_node = pkg_node_alloc(dirent->d_name);
                                meta_node->pkg.dep.is_meta = true;
                                pkg_nodes_insert_sort(meta_pkgs, meta_node);
                        }
                }
        }

        return 0;
}

static int search_pkg_nodes(const pkg_nodes_t *pkg_nodes, const char *pkg_name, string_list_t *results)
{
        char *       __pkg_name     = NULL;
        const size_t sbo_dir_offset = strlen(user_config.sbopkg_repo) + 1;

        __pkg_name = bds_string_dup(pkg_name);
        bds_string_tolower(__pkg_name);

        for (size_t i = 0; i < pkg_nodes_size(pkg_nodes); ++i) {
                const struct pkg_node *node = pkg_nodes_get_const(pkg_nodes, i);

                char *p = bds_string_dup(node->pkg.name);

                if (bds_string_contains(bds_string_tolower(p), __pkg_name)) {
                        char *sbo_dir = NULL;
                        if (node->pkg.dep.is_meta) {
                                sbo_dir = bds_string_dup_concat(2, "META/", node->pkg.name);
                        } else {
                                sbo_dir = bds_string_dup(node->pkg.sbo_dir + sbo_dir_offset);
                        }

                        string_list_insert_sort(results, sbo_dir);
                }
                free(p);
        }
        free(__pkg_name);

        return 0;
}

int run_search_command(struct pkg_graph *pkg_graph, const char *pkg_name)
{
        int            rc      = 0;
        string_list_t *results = NULL;

        results = string_list_alloc();
        rc      = search_pkg_nodes(pkg_graph_sbo_pkgs(pkg_graph), pkg_name, results);
        if (rc != 0)
                goto finish;

        /*
          Load all meta packages into the graph (as nodes)
        */
        find_all_meta_pkgs(pkg_graph_meta_pkgs(pkg_graph));
        rc = search_pkg_nodes(pkg_graph_meta_pkgs(pkg_graph), pkg_name, results);
        if (rc != 0)
                goto finish;

        for (size_t i = 0; i < string_list_size(results); ++i) {
                printf("%s\n", string_list_get_const(results, i));
        }

finish:
        if (results)
                string_list_free(&results);

        return rc;
}
