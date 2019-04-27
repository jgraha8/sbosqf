#include <assert.h>
#include <stdio.h>

#include <libbds/bds_queue.h>
#include <libbds/bds_stack.h>
#include <libbds/bds_vector.h>

#include "pkg.h"

#include "config.h"
#include "user_config.h"

struct pkg_pair {
        struct pkg *pkg;
        struct pkg *rel;
};

#define PKG(p) (((const struct pkg_pair *)p)->pkg->name)
#define REL(p) (((const struct pkg_pair *)p)->rel->name)

int compar_pkg_pair(const void *a, const void *b)
{
        int rc = 0;
        if ((rc = strcmp(PKG(a), PKG(b))) != 0)
                return rc;
        return strcmp(REL(a), REL(b));
}

int write_graph(struct pkg *pkg, enum pkg_iterator_type type, int max_dist)
{
        FILE *fp = fopen("graph.dot", "w");

        fprintf(fp, "digraph G {\n");

        struct pkg_iterator iter;

        struct bds_stack *pair_stack = bds_stack_alloc(1, sizeof(struct pkg_pair), NULL);

        for (struct pkg_node *node = pkg_iterator_begin(&iter, pkg, type, max_dist); node != NULL;
             node                  = pkg_iterator_next(&iter)) {

                struct pkg_node *cur_node = pkg_iterator_current(&iter);

                struct pkg_pair pair = {.pkg = cur_node->pkg, .rel = node->pkg};

                if (bds_stack_lsearch(pair_stack, &pair, compar_pkg_pair) == NULL)
                        bds_stack_push(pair_stack, &pair);
        }
        pkg_iterator_destroy(&iter);

        struct pkg_pair pair;

        while (bds_stack_pop(pair_stack, &pair)) {
                if (type == ITERATOR_REQUIRED) {
                        fprintf(fp, "\"%s\"->\"%s\";\n", pair.pkg->name, pair.rel->name);
                } else {
                        fprintf(fp, "\"%s\"->\"%s\";\n", pair.rel->name, pair.pkg->name);
                }
        }
        bds_stack_free(&pair_stack);

        fprintf(fp, "}\n");
        fclose(fp);

        return 0;
}

void print_revdeps(struct pkg *pkg, struct pkg_options options)
{

        struct pkg_iterator iter;

        for (struct pkg_node *node = pkg_iterator_begin(&iter, pkg, ITERATOR_PARENTS, -1); node != NULL;
             node                  = pkg_iterator_next(&iter)) {

                printf("%s:%d ", node->pkg->name, node->dist);
        }
        pkg_iterator_destroy(&iter);
}

void print_deps(struct pkg *pkg, struct pkg_options options)
{

        struct pkg_iterator iter;

        for (struct pkg_node *node = pkg_iterator_begin(&iter, pkg, ITERATOR_REQUIRED, -1); node != NULL;
             node                  = pkg_iterator_next(&iter)) {

                printf("%s:%d ", node->pkg->name, node->dist);
        }
        pkg_iterator_destroy(&iter);
}

int main(int argc, char **argv)
{
        struct pkg_graph *pkg_graph = pkg_graph_alloc();
        struct pkg_options options  = pkg_options_default();

        pkg_vector_t *sbo_pkgs = pkg_graph_sbo_pkgs(pkg_graph);

        user_config_init();

        if (pkg_load_db(sbo_pkgs) != 0) {
                assert(pkg_load_sbo(sbo_pkgs) == 0);
                pkg_create_db(sbo_pkgs);
        }

        options.recursive = true;

        pkg_load_dep(pkg_graph, "meta3", options);
        pkg_load_revdeps(pkg_graph, options);

        struct pkg *pkg        = pkg_graph_search(pkg_graph, "meta3");
        pkg_vector_t *reviewed = pkg_vector_alloc_reference();

        if (pkg_review(pkg) == 0) {
                pkg_vector_append(reviewed, pkg);
                pkg_create_reviewed(reviewed);
        }

        printf("%s deps:", pkg->name);

        print_deps(pkg, options);
        printf("\n");

        write_graph(pkg, ITERATOR_REQUIRED, -1);

        pkg_graph_free(&pkg_graph);
        pkg_vector_free(&reviewed);
        user_config_destroy();

        return 0;
}
