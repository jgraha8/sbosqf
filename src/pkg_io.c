#include <assert.h>
#include <dirent.h>
#include <sys/stat.h>

#include <libbds/bds_stack.h>

#include "mesg.h"
#include "pkg_io.h"
#include "pkg_util.h"
#include "sbo.h"
#include "user_config.h"
#include "xlimits.h"

enum block_type { NO_BLOCK, REQUIRED_BLOCK, OPTIONAL_BLOCK, BUILDOPTS_BLOCK };

static int __load_sbo(pkg_nodes_t *pkgs, const char *cur_path)
{
        static char sbo_dir[4096];
        static int  level = 0;

        int            rc     = 0;
        struct dirent *dirent = NULL;

        DIR *dp = opendir(cur_path);
        if (dp == NULL)
                return 1;

        ++level;
        assert(level >= 1);

        if (level > 2) {
                goto finish;
        }

        while ((dirent = readdir(dp)) != NULL) {
                if (*dirent->d_name == '.')
                        continue;

                if (level == 1) {
                        if (dirent->d_type == DT_DIR) {
                                char *next_dir = bds_string_dup_concat(3, cur_path, "/", dirent->d_name);
                                rc             = __load_sbo(pkgs, next_dir);
                                free(next_dir);
                                if (rc != 0)
                                        goto finish;
                        }
                } else {
                        if (dirent->d_type == DT_DIR) {
                                assert(bds_string_copyf(sbo_dir, 4096, "%s/%s", cur_path, dirent->d_name));

                                // Now check for a .info file
                                char info[256];
                                bds_string_copyf(info, sizeof(info), "%s/%s.info", sbo_dir, dirent->d_name);

                                struct stat sb;
                                if (stat(info, &sb) != 0) {
                                        perror("stat()");
                                        continue;
                                }
                                if (!S_ISREG(sb.st_mode))
                                        continue;

                                struct pkg_node *pkg_node = pkg_node_alloc(dirent->d_name);

                                pkg_init_version(&pkg_node->pkg, sbo_read_version(sbo_dir, pkg_node->pkg.name));
                                pkg_init_sbo_dir(&pkg_node->pkg, sbo_dir);
                                pkg_set_info_crc(&pkg_node->pkg);
                                pkg_nodes_append(pkgs, pkg_node);
                        }
                }
        }

finish:
        if (dp)
                closedir(dp);
        --level;

        return rc;
}

int pkg_load_sbo(pkg_nodes_t *pkgs)
{
        int rc = 0;
        if ((rc = __load_sbo(pkgs, user_config.sbopkg_repo)) != 0) {
                return rc;
        }
        bds_vector_qsort(pkgs, pkg_nodes_compar);

        return 0;
}

bool pkg_db_exists()
{
        char db_file[MAX_LINE];
        bds_string_copyf(db_file, sizeof(db_file), "%s/PKGDB", user_config.depdir);

        return file_exists(db_file);
}

static int __create_db(const char *db_file, pkg_nodes_t *pkgs)
{
        FILE *fp = fopen(db_file, "w");
        if (fp == NULL) {
                return -1;
        }

        for (size_t i = 0; i < bds_vector_size(pkgs); ++i) {
                struct pkg_node *pkg_node = *(struct pkg_node **)bds_vector_get(pkgs, i);

                if (pkg_node->pkg.name == NULL) /* Package has been removed */
                        continue;

                fprintf(fp, "%s:%s:%s:%x:%d:%d", pkg_node->pkg.name,
                        pkg_node->pkg.sbo_dir + strlen(user_config.sbopkg_repo) + 1, pkg_node->pkg.version,
                        pkg_node->pkg.info_crc, pkg_node->pkg.is_reviewed, pkg_node->pkg.is_tracked);
                fprintf(fp, "\n");
        }
        fclose(fp);

        return 0;
}

int pkg_write_db(pkg_nodes_t *pkgs)
{
        int rc = 0;

        char db_file[4096];
        bds_string_copyf(db_file, sizeof(db_file), "%s/PKGDB", user_config.depdir);

        rc = __create_db(db_file, pkgs);
        if (rc != 0) {
                mesg_error("unable to write %s\n", db_file);
        }

        return rc;
}

static int __load_db(pkg_nodes_t *pkgs, const char *db_file)
{
        FILE *fp = fopen(db_file, "r");
        assert(fp);

        char sbo_dir[256];
        char line[MAX_LINE];
        line[MAX_LINE - 1] = '\0';

        while (fgets(line, MAX_LINE, fp)) {
                char *c = NULL;
                // Get newline character
                if ((c = bds_string_rfind(line, "\n"))) {
                        *c = '\0';
                }
                bds_string_atrim(line);

                size_t num_tok = 0;
                char **tok     = NULL;

                bds_string_tokenize(line, ":", &num_tok, &tok);
                assert(num_tok == 6);

                struct pkg_node *pkg_node = pkg_node_alloc(tok[0]);

                bds_string_copyf(sbo_dir, sizeof(sbo_dir), "%s/%s", user_config.sbopkg_repo, tok[1]);
                pkg_init_sbo_dir(&pkg_node->pkg, sbo_dir);
                pkg_init_version(&pkg_node->pkg, tok[2]);
                pkg_node->pkg.info_crc    = strtol(tok[3], NULL, 16);
                pkg_node->pkg.is_reviewed = strtol(tok[4], NULL, 10);
                pkg_node->pkg.is_tracked  = strtol(tok[5], NULL, 10);

                pkg_nodes_append(pkgs, pkg_node);

                free(tok);
        }
        fclose(fp);

        return 0;
}

int pkg_load_db(pkg_nodes_t *pkgs)
{
        if (!pkg_db_exists())
                return 1;

        char db_file[4096];
        bds_string_copyf(db_file, sizeof(db_file), "%s/PKGDB", user_config.depdir);

        return __load_db(pkgs, db_file);
}

int pkg_load_all_deps(struct pkg_graph *pkg_graph, struct pkg_options options)
{
        pkg_nodes_t *pkgs[2] = {pkg_graph->sbo_pkgs, pkg_graph->meta_pkgs};

        for (size_t n = 0; n < 2; ++n) {
                // We load deps for all packages
                for (size_t i = 0; i < bds_vector_size(pkgs[n]); ++i) {
                        struct pkg_node *pkg_node = *(struct pkg_node **)bds_vector_get(pkgs[n], i);

                        if (pkg_node->pkg.name == NULL)
                                continue;

                        int rc = 0;
                        if ((rc = pkg_load_dep(pkg_graph, pkg_node->pkg.name, options)) != 0)
                                return rc;
                }
        }

        return 0;
}

int pkg_load_installed_deps(const struct slack_pkg_dbi *slack_pkg_dbi,
                            struct pkg_graph *          pkg_graph,
                            struct pkg_options          options)
{
        pkg_nodes_t *pkgs[2] = {pkg_graph->sbo_pkgs, pkg_graph->meta_pkgs};

        for (size_t n = 0; n < 2; ++n) {
                // We load deps for all packages
                for (size_t i = 0; i < bds_vector_size(pkgs[n]); ++i) {
                        struct pkg_node *pkg_node = *(struct pkg_node **)bds_vector_get(pkgs[n], i);

                        if (pkg_node->pkg.name == NULL)
                                continue;

                        if (!slack_pkg_dbi->is_installed(pkg_node->pkg.name, NULL)) {
                                continue;
                        }

                        int rc = 0;
                        if ((rc = pkg_load_dep(pkg_graph, pkg_node->pkg.name, options)) != 0)
                                return rc;
                }
        }

        return 0;
}

static int __load_dep(struct pkg_graph * pkg_graph,
                      struct pkg_node *  pkg_node,
                      struct pkg_options options,
                      pkg_nodes_t *      visit_list,
                      struct bds_stack * visit_path)
{
        int rc = 0;

        char * line     = NULL;
        size_t num_line = 0;
        char * dep_file = NULL;
        FILE * fp       = NULL;

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

                if (pkg_skip_dep_line(line))
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

int pkg_load_dep(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options options)
{
        struct pkg_node *pkg_node = pkg_graph_search(pkg_graph, pkg_name);
        if (pkg_node == NULL)
                return 1;

        struct bds_stack *visit_path = bds_stack_alloc(1, sizeof(struct pkg_node *), NULL);
        pkg_nodes_t *     visit_list = pkg_nodes_alloc_reference();

        int rc = __load_dep(pkg_graph, pkg_node, options, visit_list, visit_path);

        bds_stack_free(&visit_path);
        pkg_nodes_free(&visit_list);

        return rc;
}

/*
 * Return:
 *   -1   if error occurred
 *    0   success / no errors
 *    >0  dep file has been modified, during review (1 == PKG_DEP_REVERTED_DEFAULT, 2 == PKG_DEP_EDITED)
 */
static int __write_sqf(struct pkg_graph *          pkg_graph,
                       const struct slack_pkg_dbi *slack_pkg_dbi,
                       const char *                pkg_name,
                       struct pkg_options          options,
                       bool *                      db_dirty,
                       pkg_nodes_t *               review_skip_pkgs,
                       pkg_nodes_t *               output_pkgs)
{
        int                 rc = 0;
        struct pkg_iterator iter;

        pkg_iterator_flags_t flags    = 0;
        int                  max_dist = (options.max_dist >= 0 ? options.max_dist : (options.deep ? -1 : 1));

        if (options.revdeps) {
                flags = PKG_ITER_REVDEPS;
        } else {
                flags = PKG_ITER_DEPS;
        }

        for (struct pkg_node *node = pkg_iterator_begin(&iter, pkg_graph, pkg_name, flags, max_dist); node != NULL;
             node                  = pkg_iterator_next(&iter)) {

                if (node->pkg.dep.is_meta)
                        continue;

                if (options.check_installed && strcmp(pkg_name, node->pkg.name) != 0) {
                        const char *tag =
                            (options.check_installed & PKG_CHECK_ANY_INSTALLED ? NULL : user_config.sbo_tag);
                        if (slack_pkg_dbi->is_installed(node->pkg.name, tag)) {
                                // mesg_info("package %s is already installed: skipping\n", node->pkg.name);
                                continue;
                        }
                }

                if (pkg_nodes_bsearch_const(review_skip_pkgs, node->pkg.name) == NULL) {
                        rc = pkg_check_reviewed(&node->pkg, options.review_type, db_dirty);
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
 * Return:
 *   -1   if error occurred
 *    0   success / no errors
 */
int pkg_write_sqf(struct ostream *            os,
                  const struct slack_pkg_dbi *slack_pkg_dbi,
                  struct pkg_graph *          pkg_graph,
                  const string_list_t *       pkg_names,
                  struct pkg_options          options,
                  bool *                      db_dirty)
{
        int rc = 0;

        pkg_nodes_t *  output_pkgs      = NULL;
        string_list_t *review_skip_pkgs = string_list_alloc_reference();
        const size_t   num_pkgs         = string_list_size(pkg_names);

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

        const size_t num_output  = pkg_nodes_size(output_pkgs);
        bool         have_output = (num_output > 0);

        for (size_t i = 0; i < num_output; ++i) {
                const struct pkg_node *node = NULL;

                if (options.revdeps) {
                        node = pkg_nodes_get_const(output_pkgs, num_output - 1 - i);
                } else {
                        node = pkg_nodes_get_const(output_pkgs, i);
                }

                ostream_printf(os, "%s", pkg_output_name(options.output_mode, node->pkg.name));
                if (PKG_OUTPUT_FILE != options.output_mode) {
                        ostream_printf(os, " ");
                } else {
			write_buildopts(os, &node->pkg);
                        ostream_printf(os, "\n");
                }
        }

        if (have_output && PKG_OUTPUT_FILE != options.output_mode)
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
