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
	if( (rc = strcmp(PKG(a),PKG(b))) != 0 )
		return rc;
	return strcmp(REL(a), REL(b));
}

int write_graph(struct pkg *pkg, enum pkg_iterator_type type, int max_dist)
{
        FILE *fp = fopen("graph.dot", "w");

        fprintf(fp, "digraph G {\n");

        struct pkg_iterator iter;


	struct bds_stack *pair_stack = bds_stack_alloc(1, sizeof(struct pkg_pair), NULL);

        for (struct pkg *p = pkg_iterator_begin(&iter, pkg, type, max_dist); p != NULL;
             p             = pkg_iterator_next(&iter)) {

                struct pkg_node *pkg_node = pkg_iterator_node(&iter);

		struct pkg_pair pair = { .pkg = pkg_node->pkg, .rel = p };

                if (bds_stack_lsearch(pair_stack, &pair, compar_pkg_pair) == NULL)
                        bds_stack_push(pair_stack, &pair);
        }
        pkg_iterator_destroy(&iter);


	struct pkg_pair pair;

	while( bds_stack_pop(pair_stack, &pair) ) {
		if( type == ITERATOR_REQUIRED ) {
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

        for (struct pkg *p = pkg_iterator_begin(&iter, pkg, ITERATOR_REQUIRED, -1); p != NULL;
             p             = pkg_iterator_next(&iter)) {

                printf("%s:%d ", p->name, iter.pkg_node.dist + 1);
        }
        pkg_iterator_destroy(&iter);
}

int main(int argc, char **argv)
{
        pkg_graph_t *pkg_graph     = NULL;
        struct pkg_options options = pkg_options_default();

        user_config_init();

        if ((pkg_graph = pkg_load_db()) == NULL) {
                pkg_graph = pkg_load_sbo();
                pkg_create_db(pkg_graph);
        }

        options.recursive = false;
        pkg_load_revdeps(pkg_graph, options);

        struct pkg *pkg = pkg_graph_bsearch(pkg_graph, "ffmpeg");

        pkg_graph_t *reviewed = pkg_graph_alloc_reference();

        pkg_graph_append(reviewed, pkg);
        pkg_create_reviewed(reviewed);

        printf("%s revdeps:", pkg->name);

        print_revdeps(pkg, options);
        printf("\n");

	write_graph(pkg, ITERATOR_PARENTS, 2);

        pkg_graph_free(&pkg_graph);
        pkg_graph_free(&reviewed);
        user_config_destroy();

        return 0;
}
