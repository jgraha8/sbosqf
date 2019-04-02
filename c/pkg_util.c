#include <assert.h>
#include <stdio.h>

#include <libbds/bds_queue.h>
#include <libbds/bds_string.h>

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

/* static void destroy_dep_entry(struct dep_entry *de) { memset(de, 0, sizeof(*de)); } */

/* static dep_queue_t *dep_queue_alloc() */
/* { */
/*         dep_queue_t *queue = bds_queue_alloc(1, sizeof(struct dep_entry), (void (*)(void *))destroy_dep_entry);
 */

/*         bds_queue_set_autoresize(queue, true); */

/*         return queue; */
/* } */

/* void dep_queue_free(dep_queue_t **queue) */
/* { */
/*         if (!(*queue)) */
/*                 return; */

/*         bds_queue_free(queue); */
/* } */

/* void dep_queue_push(dep_queue_t *queue, const struct dep_entry *entry) { bds_queue_push(queue, entry); } */

/* int dep_queue_pop(dep_queue_t *queue, struct dep_entry *entry) */
/* { */
/*         if (bds_queue_pop(queue, entry) == NULL) */
/*                 return 1; */

/*         return 0; */
/* } */

bool skip_installed(const char *pkg_name, struct pkg_options options)
{
        if (options.check_installed) {
                const char *check_tag =
                    (options.check_installed & PKG_CHECK_ANY_INSTALLED ? NULL : user_config.sbo_tag);
                if (slack_pkg_is_installed(pkg_name, check_tag)) {
                        return true;
                }
        }
        return false;
}

int __load_dep(struct bds_queue *node_queue, pkg_graph_t *pkg_graph, struct pkg_options options)
{
        int rc = 0;

        struct pkg_node pkg_node;

        while (rc == 0 && bds_queue_pop(node_queue, &pkg_node)) {
                char *line      = NULL;
                size_t num_line = 0;
                char *dep_file  = NULL;
                FILE *fp        = NULL;

                struct pkg *pkg = pkg_node.pkg;
                int dist        = pkg_node.dist;

                dep_file = bds_string_dup_concat(3, user_config.depdir, "/", pkg->name);
                fp       = fopen(dep_file, "r");

                if (fp == NULL) {
                        // Create the default dep file (don't ask just do it)
                        if (create_default_dep(pkg) == NULL) {
                                rc = 1;
                                goto finish;
                        }

                        fp = fopen(dep_file, "r");
                        if (fp == NULL) {
                                rc = 1;
                                goto finish;
                        }
                }

                enum block_type block_type = NO_BLOCK;

                while (getline(&line, &num_line, fp) != -1) {
                        assert(line);

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
                                goto cycle;
                        }

                        if (*line == '-') {
                                goto cycle;
                        }

                        if (strcmp(line, "METAPKG") == 0) {
                                pkg->dep.is_meta = true;
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
                         * Recursive processing will occur on meta packages since they act as "include" files.  We
                         * only
                         * check the recursive flag if the dependency file is not marked as a meta package.
                         */
                        if (!pkg->dep.is_meta && !options.recursive && dist > 0)
                                goto finish;

                        switch (block_type) {
                        case OPTIONAL_BLOCK:
                                if (!options.optional)
                                        break;
                        case REQUIRED_BLOCK: {
                                if (skip_installed(line, options))
                                        break;

                                // assert(load_dep_file(pkg_graph, line, options) == 0);
                                struct pkg *req = pkg_graph_bsearch(pkg_graph, line);
                                assert(req);

                                struct pkg_node new_node = pkg_node_create(req, dist + 1);
                                bds_queue_push(node_queue, &new_node);

                                if (options.revdeps)
                                        pkg_append_parent(req, pkg);
                                pkg_append_required(pkg, req);

                        } break;
                        case BUILDOPTS_BLOCK: {
                                char *buildopt = bds_string_dup(bds_string_atrim(line));
                                pkg_append_buildopts(pkg, buildopt);
                        } break;
                        default:
                                fprintf(stderr, "%s(%d): badly formatted dependency file %s\n", __FILE__, __LINE__,
                                        dep_file);
                                exit(EXIT_FAILURE);
                        }

                cycle:
                        free(line);
                        line     = NULL;
                        num_line = 0;
                }

        finish:
                if (line != NULL) {
                        free(line);
                }

                if (fp)
                        fclose(fp);
                free(dep_file);

		pkg_node_destroy(&pkg_node);
        }

        return rc;
}

int load_dep_file(pkg_graph_t *pkg_graph, const char *pkg_name, struct pkg_options options)
{
        struct pkg *pkg = pkg_graph_bsearch(pkg_graph, pkg_name);
        if (pkg == NULL)
                return 1;

        struct bds_queue *node_queue = bds_queue_alloc(1, sizeof(struct pkg_node), NULL);
        struct pkg_node pkg_node = pkg_node_create(pkg, 0);

        bds_queue_push(node_queue, &pkg_node);

        int rc = __load_dep(node_queue, pkg_graph, options);

        bds_queue_free(&node_queue);

        return rc;
}

const char *create_default_dep(struct pkg *pkg)
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
