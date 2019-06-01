#include <assert.h>
#include <stdio.h>

#include <libbds/bds_queue.h>
#include <libbds/bds_stack.h>
#include <libbds/bds_vector.h>

#include "pkg.h"

#include "config.h"
#include "user_config.h"

#if 0
#define NODE(p) (((const struct pkg_pair *)p)->pkg->name)
#define REL(p) (((const struct pkg_pair *)p)->rel->name)

int compar_pkg_pair(const void *a, const void *b)
{
        int rc = 0;
        if ((rc = strcmp(PKG(a), PKG(b))) != 0)
                return rc;
        return strcmp(REL(a), REL(b));
}

#endif

const char *node_style(const struct pkg_node *node)
{
	static const char *no_style = "";
	static const char *meta_style = " [style=dashed]";

	if( node->pkg.dep.is_meta )
		return meta_style;
	return no_style;
}

void insert_node(pkg_nodes_t *node_list, struct pkg_node *node)
{
	if( pkg_nodes_bsearch(node_list, node->pkg.name) )
		return;

	pkg_nodes_insert_sort(node_list, node);
}

void write_node_list(FILE *fp, const pkg_nodes_t *node_list)
{
	const size_t n = pkg_nodes_size(node_list);

	for( size_t i=0; i<n; ++i ) {
		const struct pkg_node *node = pkg_nodes_get_const(node_list, i);
		if( node->pkg.dep.is_meta )
			fprintf(fp, "\t\"%s\"%s;\n", node->pkg.name, node_style(node));
	}
}

int write_graph(struct pkg_graph *pkg_graph, const char *pkg_name, pkg_iterator_flags_t flags, int max_dist)
{
	if( pkg_graph_search(pkg_graph, pkg_name) == NULL)
		return -1;

	flags |= PKG_ITER_REQ_NEAREST;

	pkg_nodes_t *node_list = pkg_nodes_alloc_reference();	
	
	FILE *fp = fopen("graph.dot", "w");
        fprintf(fp, "digraph G {\n");
	
        struct pkg_iterator iter;
        for (struct pkg_node *edge_node   = pkg_iterator_begin(&iter, pkg_graph, pkg_name, flags, max_dist);
             edge_node != NULL; edge_node = pkg_iterator_next(&iter)) {

                struct pkg_node *node = pkg_iterator_node(&iter);
                if (node == NULL)
                        continue;

		if( flags & PKG_ITER_REVDEPS ) {
			fprintf(fp, "\t\"%s\" -> \"%s\";\n", edge_node->pkg.name, node->pkg.name);
		} else {
			fprintf(fp, "\t\"%s\" -> \"%s\";\n", node->pkg.name, edge_node->pkg.name);
		}

		insert_node(node_list, node);
		insert_node(node_list, edge_node);
        }
        pkg_iterator_destroy(&iter);

	fprintf(fp, "\n");

	write_node_list(fp, node_list);
	pkg_nodes_free(&node_list);

        fprintf(fp, "}\n");
        fclose(fp);

        return 0;
}


int __print_deps(struct pkg_graph *pkg_graph, const char *pkg_name, pkg_iterator_flags_t flags, int dist)
{

        struct pkg_iterator iter;

	printf("%s -> ", pkg_name);
        for (struct pkg_node *node = pkg_iterator_begin(&iter, pkg_graph, pkg_name, flags, dist); node != NULL;
             node                  = pkg_iterator_next(&iter)) {
               if( node->pkg.dep.is_meta )
			continue;

		if( strcmp(pkg_name, node->pkg.name) == 0 )
			continue;
		
		printf("%s:%d ", node->pkg.name, node->dist);
        }
	printf("\n");
        pkg_iterator_destroy(&iter);

	return 0;
}

int print_revdeps(struct pkg_graph *pkg_graph, const char *pkg_name, int dist)
{
	return __print_deps(pkg_graph, pkg_name, PKG_ITER_REVDEPS, dist);
}

int print_deps(struct pkg_graph *pkg_graph, const char *pkg_name)
{
	return __print_deps(pkg_graph, pkg_name, PKG_ITER_DEPS, -1);
}


int main(int argc, char **argv)
{
        struct pkg_graph *pkg_graph = pkg_graph_alloc();
        struct pkg_options options  = pkg_options_default();

        pkg_nodes_t *sbo_pkgs = pkg_graph_sbo_pkgs(pkg_graph);

        user_config_init();

        if (!pkg_db_exists()) {
                pkg_load_sbo(sbo_pkgs);
                pkg_create_db(sbo_pkgs);
                pkg_create_default_deps(sbo_pkgs);
        } else {
                pkg_load_db(sbo_pkgs);
        }

        options.recursive = true;
	options.revdeps = true;
	
        pkg_load_all_deps(pkg_graph, options);	
        pkg_load_dep(pkg_graph, "meta3", options);
        /* pkg_load_dep(pkg_graph, "virt-manager", options);	 */


	print_revdeps(pkg_graph, "colord", 1);
#if 0
        struct pkg *pkg        = pkg_graph_search(pkg_graph, "virt-manager");
        pkg_vector_t *reviewed = pkg_vector_alloc_reference();

        if (pkg_review(pkg) == 0) {
                pkg_vector_append(reviewed, pkg);
                pkg_create_reviewed(reviewed);
        }

        printf("%s deps:", pkg->name);

        print_deps(pkg, options);
        printf("\n");

#endif

        write_graph(pkg_graph, "ffmpeg", PKG_ITER_REVDEPS, -1);
	print_deps(pkg_graph, "virt-manager");

#if 0	
        pkg_vector_free(&reviewed);
#endif
        pkg_graph_free(&pkg_graph);
        user_config_destroy();

        return 0;
}
