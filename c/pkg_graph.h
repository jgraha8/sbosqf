#ifndef __PKG_GRAPH_H__
#define __PKG_GRAPH_H__

#include "pkg.h"

enum pkg_color {
        COLOR_WHITE = 0, //
        COLOR_GREY,
        COLOR_BLACK
};

struct pkg_node {
        struct pkg pkg;
        int dist;
        enum pkg_color color;
        int edge_index;
};

struct pkg_node *pkg_node_alloc(const char *name);
void pkg_node_free(struct pkg_node **pkg_node);
struct pkg_node pkg_node_create(struct pkg *pkg, int dist);
void pkg_node_destroy(struct pkg_node *pkg_node);
int pkg_node_compar(const void *a, const void *b);

typedef struct bds_vector pkg_nodes_t;

pkg_nodes_t *pkg_nodes_alloc_reference();
pkg_nodes_t *pkg_nodes_alloc();

size_t pkg_nodes_size(const pkg_nodes_t *nodes);
const struct pkg_node *pkg_nodes_get_const(const pkg_nodes_t *nodes, size_t i);

void pkg_nodes_free(pkg_nodes_t **pl);
void pkg_nodes_append(pkg_nodes_t *pl, struct pkg_node *pkg);
void pkg_nodes_insert_sort(pkg_nodes_t *pkg_nodes, struct pkg_node *pkg);
int pkg_nodes_remove(pkg_nodes_t *pl, const char *pkg_name);
struct pkg_node *pkg_nodes_lsearch(pkg_nodes_t *pl, const char *name);
const struct pkg_node *pkg_nodes_lsearch_const(const pkg_nodes_t *pl, const char *name);
struct pkg_node *pkg_nodes_bsearch(pkg_nodes_t *pl, const char *name);
const struct pkg_node *pkg_nodes_bsearch_const(const pkg_nodes_t *pl, const char *name);
int pkg_nodes_compar(const void *a, const void *b);

struct pkg_graph {
        pkg_nodes_t *sbo_pkgs;
        pkg_nodes_t *meta_pkgs;
};

struct pkg_graph *pkg_graph_alloc();
struct pkg_graph *pkg_graph_alloc_reference();
void pkg_graph_free(struct pkg_graph **pkg_graph);
pkg_nodes_t *pkg_graph_sbo_pkgs(struct pkg_graph *pkg_graph);
pkg_nodes_t *pkg_graph_assign_sbo_pkgs(struct pkg_graph *pkg_graph, pkg_nodes_t *sbo_pkgs);
pkg_nodes_t *pkg_graph_meta_pkgs(struct pkg_graph *pkg_graph);
int pkg_graph_load_sbo(struct pkg_graph *pkg_graph);
struct pkg_node *pkg_graph_search(struct pkg_graph *pkg_graph, const char *pkg_name);
void pkg_graph_clear_markers(struct pkg_graph *pkg_graph);

#define PKG_ITER_DEPS          0x00
#define PKG_ITER_REVDEPS       0x01
#define PKG_ITER_REQ_NEAREST   0x02
#define PKG_ITER_FORW          0x04
#define PKG_ITER_METAPKG_DIST  0x08

typedef int pkg_iterator_flags_t;

// enum pkg_iterator_type { ITERATOR_REQUIRED, ITERATOR_PARENTS };

struct pkg_iterator {
        pkg_iterator_flags_t flags;
        // struct pkg_node pkg_node;
        struct pkg_node *cur_node;
	struct pkg_node *edge_node;
	pkg_nodes_t *edge_nodes;
        // int pkgs_index;
        // struct pkg **pkgp;
        struct bds_stack *visit_nodes;
	struct pkg_node *(*next)(struct pkg_iterator *iter);
        int max_dist;
};

struct pkg_node *pkg_iterator_begin(struct pkg_iterator *iter, struct pkg_graph *pkg_graph, const char *pkg_name,
                                    pkg_iterator_flags_t flags, int max_dist);
struct pkg_node *pkg_iterator_node(struct pkg_iterator *iter);
struct pkg_node *pkg_iterator_next(struct pkg_iterator *iter);
void pkg_iterator_destroy(struct pkg_iterator *iter);

#endif
