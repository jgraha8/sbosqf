#ifndef __PKG_H__
#define __PKG_H__

#include <stdint.h>

#include <zlib.h>

#include <libbds/bds_vector.h> 
#include <libbds/bds_string.h>

#define PKG_CHECK_INSTALLED     0x1
#define PKG_CHECK_ANY_INSTALLED 0x2

typedef struct bds_vector pkg_list_t;
typedef struct bds_vector buildopts_t;

struct pkg_dep {
        pkg_list_t *required;
	pkg_list_t *parents;
        buildopts_t *buildopts;
        bool is_meta;
};

struct pkg {
        char *name;
        char *sbo_dir;
        struct pkg_dep dep;
	uint32_t info_crc; /// CRC of README and REQUIRED list in .info
        // struct pkg_sbo *sbo;
};

struct pkg_options {
	bool recursive;
	bool optional;
	bool revdeps;
	int check_installed;	
};


struct pkg_options pkg_options_default();

void pkg_destroy(struct pkg *pkg);

struct pkg *pkg_alloc(const char *name);

void pkg_free(struct pkg **pkg);

struct pkg *pkg_clone_nodep(const struct pkg *pkg);

void pkg_init_sbo_dir(struct pkg *pkg, const char *sbo_dir);

int pkg_set_info_crc(struct pkg *pkg);

void pkg_append_required(struct pkg *pkg, struct pkg *req);

void pkg_append_parent(struct pkg *pkg, struct pkg *parent);

void pkg_append_buildopts(struct pkg *pkg, char *bopt);

pkg_list_t *pkg_list_alloc_reference();
pkg_list_t *pkg_list_alloc();
void pkg_list_free(pkg_list_t **pl);
void pkg_list_append(pkg_list_t *pl, struct pkg *pkg);
int pkg_list_remove(pkg_list_t *pl, const char *pkg_name);
struct pkg *pkg_list_lsearch(pkg_list_t *pl, const char *name);
struct pkg *pkg_list_bsearch(pkg_list_t *pl, const char *name);

pkg_list_t *pkg_load_sbo();

bool pkg_db_exists();
bool pkg_reviewed_exists();

pkg_list_t *pkg_load_db();
pkg_list_t *pkg_load_reviewed();

int pkg_create_db(pkg_list_t *pkg_list);
int pkg_create_reviewed(pkg_list_t *pkg_list);

int pkg_load_dep(pkg_list_t *pkg_list, const char *pkg_name, struct pkg_options options);
int pkg_load_revdeps(pkg_list_t *pkg_list, struct pkg_options options);

int pkg_review(struct pkg *pkg);

int compar_pkg_list(const void *a, const void *b);
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
