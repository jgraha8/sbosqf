#ifndef __PKG_H__
#define __PKG_H__

#include <stdint.h>

#include <zlib.h>

#include <libbds/bds_string.h>
#include <libbds/bds_vector.h>

#define PKG_CHECK_INSTALLED 0x1
#define PKG_CHECK_ANY_INSTALLED 0x2

typedef struct bds_vector pkg_nodes_t;
typedef struct bds_vector buildopts_t;

struct pkg;
struct pkg_node;

pkg_nodes_t *pkg_nodes_alloc_reference();
pkg_nodes_t *pkg_nodes_alloc();

size_t pkg_nodes_size(const pkg_nodes_t *nodes);
const struct pkg_node *pkg_nodes_get_const(const pkg_nodes_t *nodes, size_t i);

void pkg_nodes_free(pkg_nodes_t **pl);
void pkg_nodes_append(pkg_nodes_t *pl, struct pkg_node *pkg);
void pkg_nodes_insert_sort(pkg_nodes_t *pkg_nodes, struct pkg_node *pkg);
int pkg_nodes_remove(pkg_nodes_t *pl, const char *pkg_name);
struct pkg_node *pkg_nodes_lsearch(pkg_nodes_t *pl, const char *name);
struct pkg_node *pkg_nodes_bsearch(pkg_nodes_t *pl, const char *name);
int pkg_nodes_compar(const void *a, const void *b);

enum pkg_color {
        COLOR_WHITE = 0, //
        COLOR_GREY,
        COLOR_BLACK
};

struct pkg_dep {
        pkg_nodes_t *required;
        pkg_nodes_t *parents;
        buildopts_t *buildopts;
        bool is_meta;
};

struct pkg {
        char *name;
        char *sbo_dir;
        struct pkg_dep dep;
        uint32_t info_crc; /// CRC of README and REQUIRED list in .info
        // struct pkg_sbo *sbo;
	bool owner;
};

struct pkg_node {
        struct pkg pkg;
        int dist;
        enum pkg_color color;
        int edge_index;
};

struct pkg pkg_create(const char *name);

void pkg_destroy(struct pkg *pkg);

void pkg_copy_nodep(struct pkg *pkg_dst, const struct pkg *pkg_src);

void pkg_init_sbo_dir(struct pkg *pkg, const char *sbo_dir);

int pkg_set_info_crc(struct pkg *pkg);

void pkg_append_required(struct pkg *pkg, struct pkg *req);

void pkg_append_parent(struct pkg *pkg, struct pkg *parent);

void pkg_append_buildopts(struct pkg *pkg, char *bopt);

struct pkg_node *pkg_node_alloc(const char *name);

void pkg_node_free(struct pkg_node **pkg_node);

struct pkg_options {
        bool recursive;
        bool optional;
        bool revdeps;
        int check_installed;
};

struct pkg_options pkg_options_default();

struct pkg_graph {
        pkg_nodes_t *sbo_pkgs;
        pkg_nodes_t *meta_pkgs;
};

struct pkg_graph *pkg_graph_alloc();
struct pkg_graph *pkg_graph_alloc_reference();
void pkg_graph_free(struct pkg_graph **pkg_graph);
pkg_nodes_t *pkg_graph_sbo_pkgs(struct pkg_graph *pkg_graph);
pkg_nodes_t *pkg_graph_meta_pkgs(struct pkg_graph *pkg_graph);
int pkg_graph_load_sbo(struct pkg_graph *pkg_graph);
struct pkg_node *pkg_graph_search(struct pkg_graph *pkg_graph, const char *pkg_name);
void pkg_graph_clear_markers(struct pkg_graph *pkg_graph);

// pkg_nodes_t *pkg_load_sbo();

bool pkg_db_exists();
bool pkg_reviewed_exists();

int pkg_create_db(pkg_nodes_t *pkgs);
int pkg_create_reviewed(pkg_nodes_t *pkgs);

int pkg_create_default_deps(pkg_nodes_t *pkgs);

int pkg_load_db(pkg_nodes_t *pkgs);
int pkg_load_reviewed(pkg_nodes_t *pkgs);
int pkg_load_sbo(pkg_nodes_t *pkgs);

int pkg_load_dep(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options options);
int pkg_load_all_deps(struct pkg_graph *pkg_graph, struct pkg_options options);

int pkg_review(struct pkg *pkg);

struct pkg_node pkg_node_create(struct pkg *pkg, int dist);
void pkg_node_destroy(struct pkg_node *pkg_node);

int pkg_node_compar(const void *a, const void *b);

#define PKG_ITER_DEPS        0x0
#define PKG_ITER_REVDEPS     0x1
#define PKG_ITER_REQ_NEAREST 0x2

typedef int pkg_iterator_flags_t;

// enum pkg_iterator_type { ITERATOR_REQUIRED, ITERATOR_PARENTS };

struct pkg_iterator {
        pkg_iterator_flags_t flags;
        // struct pkg_node pkg_node;
        struct pkg_node *cur_node;
        pkg_nodes_t *edge_nodes;
        // int pkgs_index;
        // struct pkg **pkgp;
        struct bds_stack *visit_nodes;
        int max_dist;
};

struct pkg_node *pkg_iterator_begin(struct pkg_iterator *iter, struct pkg_graph *pkg_graph, const char *pkg_name,
                                    pkg_iterator_flags_t flags, int max_dist);
struct pkg_node *pkg_iterator_node(struct pkg_iterator *iter);
struct pkg_node *pkg_iterator_next(struct pkg_iterator *iter);
void pkg_iterator_destroy(struct pkg_iterator *iter);

#endif

/* typedef struct bds_vector dep_list_t; */

/* enum graph_color { GRAPH_WHITE = 0, GRAPH_GREY, GRAPH_BLACK }; */

/* struct graph_node { */
/*         char *name; */
/*         int key; */
/*         enum graph_color color; */
/*         graph_nodes_t neighbors; */
/* }; */

/* struct graph { */
/*         graph_nodes_t nodes; */
/* }; */

// For a given package we assume we have a dep tree

// For a given set of packages we have a dep graph

// We may process the graph by traversing depth first, until a dep with no
// dependencies are found (i.e., neighbors). Only a parent package has neighbors
// since this is a directed graph

// -- that package may be built
// -- we may also check if the package is already installed (skip if need be)

// For parents we create a set of parent package nodes in the graph. For each
// parent, we traverse its dep tree and add packages (as graph nodes) if
// 1. the dependency is not installed or
// 2. the package version is greater than what is installed (update)
// 3. the node is not already in the node list

// For each dependency added to the graph or that is already in the graph, for a
// given parent, we update the parent's neighbors with the graph node.

// The graph may then be processed as usual. We can change the color
// of the graph node, once it is visited. This will allow us to check
// if we have a recusive dependency. The may also be checked by making
// sure a parent is not in a dependent neighbor list.

// Suppose we color all graph nodes black that have been added to an install
// queue. Then a node may be added to an install queue if it has no neighbors or
// if all of its neighbors have been colored black.
