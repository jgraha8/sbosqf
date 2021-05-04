#include "io.h"

/*
 * Return:
 *   -1   if error occurred
 *    0   success / no errors
 */
int io_write_sqf(struct ostream *os, const struct slack_pkg_dbi *slack_pkg_dbi, struct pkg_graph *pkg_graph,
		  const string_list_t *pkg_names, struct pkg_options options, bool *db_dirty)
{
        int rc = 0;

        pkg_nodes_t *output_pkgs        = NULL;
        string_list_t *review_skip_pkgs = string_list_alloc_reference();
        const size_t num_pkgs           = string_list_size(pkg_names);

        /* if (options.revdeps) { */
        /*         revdeps_pkgs = bds_stack_alloc(1, sizeof(struct pkg), NULL); */
        /* } */
        output_pkgs = pkg_nodes_alloc_reference();

        for (size_t i = 0; i < num_pkgs; ++i) {
                rc = 0;

                while (1) {
                        rc = __write_sqf(pkg_graph, slack_pkg_dbi,
                                         string_list_get_const(pkg_names, i) /* pkg_name */, options, db_dirty,
                                         review_skip_pkgs, output_pkgs);

                        if (rc > 0) {
                                /* A dependency file was modified during review */
                                continue;
                        }

                        if (rc < 0) { /* Error occurred */
                                goto finish;
                        }
                        break;
                }
        }

        const size_t num_output = pkg_nodes_size(output_pkgs);
        bool have_output        = (num_output > 0);

        for (size_t i = 0; i < num_output; ++i) {
                const struct pkg_node *node = NULL;

                if (options.revdeps) {
                        node = pkg_nodes_get_const(output_pkgs, num_output - 1 - i);
                } else {
                        node = pkg_nodes_get_const(output_pkgs, i);
                }

                ostream_printf(os, "%s", pkg_output_name(options.output_mode, node->pkg.name));
                if (ostream_is_console_stream(os)) {
                        ostream_printf(os, " ");
                } else {
                        write_buildopts(os, &node->pkg);
                        ostream_printf(os, "\n");
                }
        }

        if (have_output && ostream_is_console_stream(os))
                ostream_printf(os, "\n");

finish:
        if (review_skip_pkgs) {
                string_list_free(&review_skip_pkgs);
        }
        if (output_pkgs) {
                pkg_nodes_free(&output_pkgs);
        }

        return rc;
}

static int __load_dep(struct pkg_graph *pkg_graph, struct pkg_node *pkg_node, struct pkg_options options,
		      pkg_nodes_t *visit_list, struct bds_stack *visit_path)
{
        int rc = 0;

        char *line      = NULL;
        size_t num_line = 0;
        char *dep_file  = NULL;
        FILE *fp        = NULL;

        dep_file = bds_string_dup_concat(3, user_config.depdir, "/", pkg_node->pkg.name);
        fp       = fopen(dep_file, "r");

        if (fp == NULL) {
                // Create the default dep file (don't ask just do it)
                if (create_default_dep_verbose(&pkg_node->pkg) == NULL) {
                        rc = 1;
                        goto finish;
                }

                fp = fopen(dep_file, "r");
                if (fp == NULL) {
                        rc = 1;
                        goto finish;
                }
        }
        // pkg_node->color = COLOR_GREY;
        pkg_nodes_insert_sort(visit_list, pkg_node);
        bds_stack_push(visit_path, &pkg_node);

        enum block_type block_type = NO_BLOCK;

        while (getline(&line, &num_line, fp) != -1) {
                assert(line);

                if (skip_dep_line(line))
                        goto cycle;

                if (strcmp(line, "METAPKG") == 0) {
                        assert(pkg_node->pkg.dep.is_meta);
                        goto cycle;
                }

                if (strcmp(line, "REQUIRED:") == 0) {
                        block_type = REQUIRED_BLOCK;
                        goto cycle;
                }

                if (strcmp(line, "OPTIONAL:") == 0) {
                        block_type = OPTIONAL_BLOCK;
                        goto cycle;
                }

                if (strcmp(line, "BUILDOPTS:") == 0) {
                        block_type = BUILDOPTS_BLOCK;
                        goto cycle;
                }

                if (block_type == OPTIONAL_BLOCK && !options.optional)
                        goto cycle;

                /*
                 * Recursive processing will occur on meta packages since they act as "include" files. We only
                 * check the recursive flag if the dependency file is not marked as a meta package.
                 */
                if (!pkg_node->pkg.dep.is_meta && !options.recursive)
                        goto finish;

                switch (block_type) {
                case OPTIONAL_BLOCK:
                        if (!options.optional)
                                break;
                case REQUIRED_BLOCK: {
                        // if (skip_installed(line, options))
                        //        break;

                        struct pkg_node *req_node = pkg_graph_search(pkg_graph, line);

                        if (req_node == NULL) {
                                mesg_warn("%s no longer in repository but included by %s\n", line,
                                          pkg_node->pkg.name);
                                break;
                        }

                        if (bds_stack_lsearch(visit_path, &req_node, pkg_nodes_compar)) {
                                mesg_error("cyclic dependency found: %s <--> %s\n", pkg_node->pkg.name,
                                           req_node->pkg.name);
                                exit(EXIT_FAILURE);
                        }

                        if (options.revdeps)
                                pkg_insert_parent(&req_node->pkg, pkg_node);

                        pkg_insert_required(&pkg_node->pkg, req_node);

                        /* Avoid revisiting nodes more than once */
                        if (pkg_nodes_bsearch_const(visit_list, req_node->pkg.name) == NULL) {
                                __load_dep(pkg_graph, req_node, options, visit_list, visit_path);
                        }

                } break;
                case BUILDOPTS_BLOCK: {
                        char *buildopt = bds_string_atrim(line);
                        pkg_append_buildopts(&pkg_node->pkg, buildopt);
                } break;
                default:
                        mesg_error("%s(%d): badly formatted dependency file %s\n", __FILE__, __LINE__, dep_file);
                        exit(EXIT_FAILURE);
                }

        cycle:
                free(line);
                line     = NULL;
                num_line = 0;
        }

        struct pkg_node *last_node;
finish:
        // pkg_node->color = COLOR_BLACK;
        assert(bds_stack_pop(visit_path, &last_node) && pkg_node == last_node);

        if (line != NULL) {
                free(line);
        }

        if (fp)
                fclose(fp);
        free(dep_file);

        return rc;
}

int io_load_dep(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options options)
{
        struct pkg_node *pkg_node = pkg_graph_search(pkg_graph, pkg_name);
        if (pkg_node == NULL)
                return 1;

        struct bds_stack *visit_path = bds_stack_alloc(1, sizeof(struct pkg_node *), NULL);
        pkg_nodes_t *visit_list      = pkg_nodes_alloc_reference();

        int rc = __load_dep(pkg_graph, pkg_node, options, visit_list, visit_path);

        bds_stack_free(&visit_path);
        pkg_nodes_free(&visit_list);

        return rc;
}
