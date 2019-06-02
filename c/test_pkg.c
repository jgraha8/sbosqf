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
	static char style[256];
	
	static const char *default_style = "style=filled";
	static const char *meta_style = " style=dashed";

	static const char *color_white = "fillcolor=white, fontcolor=black";
	static const char *color_grey = " fillcolor=grey, fontcolor=black";
	static const char *color_black = " fillcolor=black, fontcolor=white";

	const char *sp = default_style;
	const char *cp = color_white;

	if( node->pkg.dep.is_meta )
		 sp = meta_style;
	/* if( node->color == COLOR_GREY) { */
	/* 	cp = color_grey; */
	/* } else if (node->color == COLOR_BLACK) { */
	/* 	cp = color_black; */
	/* } */
	bds_string_copyf(style, sizeof(style), " [%s, %s]", sp, cp);
	
	return style;
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
		fprintf(fp, "\t\"%s\"%s;\n", node->pkg.name, node_style(node));
	}
}

int write_graph(struct pkg_graph *pkg_graph, const char *pkg_name, pkg_iterator_flags_t flags, int max_dist)
{
	if( pkg_graph_search(pkg_graph, pkg_name) == NULL)
		return -1;

	flags |= PKG_ITER_REQ_NEAREST | PKG_ITER_METAPKG_DIST;

	pkg_nodes_t *node_list = pkg_nodes_alloc_reference();

	char fname[256];

	bds_string_copyf(fname, sizeof(fname), "graph.dot");
	
	FILE *fp = fopen(fname, "w");
        fprintf(fp, "digraph G {\n");

        struct pkg_iterator iter;
        for (struct pkg_node *edge_node   = pkg_iterator_begin(&iter, pkg_graph, pkg_name, flags, max_dist);
             edge_node != NULL; edge_node = pkg_iterator_next(&iter)) {
		
                struct pkg_node *node = pkg_iterator_node(&iter);
                if (node == NULL)
                        continue;

		if( flags & PKG_ITER_REVDEPS ) {
			fprintf(fp, "\t\"%s\" -> \"%s\"", edge_node->pkg.name, node->pkg.name);
		} else {
			fprintf(fp, "\t\"%s\" -> \"%s\"", node->pkg.name, edge_node->pkg.name);
		}

		if( edge_node->dist >= node->dist ) {
			// fprintf(fp, " [label=%d]", node->dist + 1);
			fprintf(fp, " [label=%d]", edge_node->dist);			
		} else {
			fprintf(fp, " [style=dashed]");
		}
		fprintf(fp, ";\n");

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
               /* if( node->pkg.dep.is_meta ) */
	       /* 		continue; */

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
                pkg_write_db(sbo_pkgs);
                pkg_create_default_deps(sbo_pkgs);
        } else {
                pkg_load_db(sbo_pkgs);
        }

        options.recursive = true;
	options.revdeps = true;
	
        //pkg_load_all_deps(pkg_graph, options);	
        pkg_load_dep(pkg_graph, "meta3", options);

	print_deps(pkg_graph, "meta3");
	fflush(stdout);

        write_graph(pkg_graph, "meta3", PKG_ITER_DEPS, -1);	
	return 0;
        /* pkg_load_dep(pkg_graph, "virt-manager", options);	 */


//	print_revdeps(pkg_graph, "colord", 1);

//	struct pkg_node *colord = pkg_graph_search(pkg_graph, "colord");
//	pkg_review(&colord->pkg);

	pkg_nodes_t *reviewed = pkg_nodes_alloc_reference();

	bool reviewed_modified = false;
	pkg_load_reviewed(reviewed);

	struct pkg_node *virt_manager = pkg_graph_search(pkg_graph, "virt-manager");
	struct pkg_node *r_virt_manager = pkg_nodes_bsearch(reviewed, "virt-manager");

	bool review_pkg = false;
	if( r_virt_manager == NULL ) {
		review_pkg = true;
	} else {
		if( r_virt_manager->pkg.info_crc != virt_manager->pkg.info_crc ) {
			pkg_nodes_remove(reviewed, virt_manager->pkg.name);
			reviewed_modified = true;
			review_pkg = true;
		}
	}
			
	if( review_pkg ) {
		if( pkg_review(&virt_manager->pkg) == 0 ) {
			pkg_nodes_insert_sort(reviewed, virt_manager);
			reviewed_modified = true;
		}
	}
	
	if( reviewed_modified )
		pkg_write_reviewed(reviewed);

	pkg_nodes_free(&reviewed);

        write_graph(pkg_graph, "virt-manager", PKG_ITER_DEPS, -1);
//	print_deps(pkg_graph, "virt-manager");

#if 0	
        pkg_vector_free(&reviewed);
#endif
        pkg_graph_free(&pkg_graph);
        user_config_destroy();

        return 0;
}
