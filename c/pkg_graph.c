#include <assert.h>
#include <stdio.h>

#include <libbds/bds_stack.h>

#include "pkg_graph.h"
#include "pkg_util.h"

int pkg_nodes_compar(const void *a, const void *b)
{
        const struct pkg_node *pa = *(const struct pkg_node **)a;
        const struct pkg_node *pb = *(const struct pkg_node **)b;

        if (pa->pkg.name == pb->pkg.name)
                return 0;

        if (pa->pkg.name == NULL)
                return 1;
        if (pb->pkg.name == NULL)
                return -1;

        return strcmp(pa->pkg.name, pb->pkg.name);
}

/*
  pkg_node
 */
struct pkg_node *pkg_node_alloc(const char *name)
{
        struct pkg_node *pkg_node = calloc(1, sizeof(*pkg_node));

        pkg_node->pkg   = pkg_create(name);
        pkg_node->state = bds_vector_alloc(8, sizeof(struct pkg_node_state), NULL);

        return pkg_node;
}

void pkg_node_destroy(struct pkg_node *pkg_node)
{
        bds_vector_free(&pkg_node->state);
        pkg_destroy(&pkg_node->pkg);
        memset(&pkg_node, 0, sizeof(pkg_node));
}

void pkg_node_free(struct pkg_node **pkg_node)
{
        if (*pkg_node == NULL)
                return;

        pkg_node_destroy(*pkg_node);
        free(*pkg_node);
        *pkg_node = NULL;
}

struct pkg_node_state *pkg_node_state(struct pkg_node *pkg_node, size_t graph_id)
{
        if (graph_id > bds_vector_size(pkg_node->state)) {
                bds_vector_resize(pkg_node->state, graph_id);
        }

        return (struct pkg_node_state *)bds_vector_get(pkg_node->state, graph_id);
}

const struct pkg_node_state *pkg_node_state_const(struct pkg_node *pkg_node, size_t graph_id)
{
        if (graph_id > bds_vector_size(pkg_node->state)) {
                bds_vector_resize(pkg_node->state, graph_id);
        }

        return (const struct pkg_node_state *)bds_vector_get_const(pkg_node->state, graph_id);
}

void pkg_node_clear_markers(struct pkg_node *pkg_node, size_t graph_id)
{
        struct pkg_node_state *state = pkg_node_state(pkg_node, graph_id);

        state->dist       = -1;
        state->color      = COLOR_WHITE;
        state->edge_index = 0;
}

void pkg_graph_clear_markers(struct pkg_graph *pkg_graph, size_t graph_id)
{
        size_t n = 0;

        n = bds_vector_size(pkg_graph->sbo_pkgs);
        for (size_t i = 0; i < n; ++i) {
                struct pkg_node *pkg_node = *(struct pkg_node **)bds_vector_get(pkg_graph->sbo_pkgs, i);
                pkg_node_clear_markers(pkg_node, graph_id);
        }

        n = bds_vector_size(pkg_graph->meta_pkgs);
        for (size_t i = 0; i < n; ++i) {
                struct pkg_node *pkg_node = *(struct pkg_node **)bds_vector_get(pkg_graph->meta_pkgs, i);
                pkg_node_clear_markers(pkg_node, graph_id);
        }
}

pkg_nodes_t *pkg_nodes_alloc_reference() { return bds_vector_alloc(1, sizeof(struct pkg_node *), NULL); }
pkg_nodes_t *pkg_nodes_alloc()
{
        return bds_vector_alloc(1, sizeof(struct pkg_node *), (void (*)(void *))pkg_node_free);
}

void pkg_nodes_free(pkg_nodes_t **pl) { bds_vector_free(pl); }

struct pkg_node *pkg_nodes_get(pkg_nodes_t *nodes, size_t i)
{
        return (struct pkg_node *)pkg_nodes_get_const(nodes, i);
}

const struct pkg_node *pkg_nodes_get_const(const pkg_nodes_t *nodes, size_t i)
{
        const struct pkg_node **nodep = (const struct pkg_node **)bds_vector_get((pkg_nodes_t *)nodes, i);

        if (nodep == NULL)
                return NULL;

        return *nodep;
}

size_t pkg_nodes_size(const pkg_nodes_t *nodes) { return bds_vector_size(nodes); }

void pkg_nodes_append(pkg_nodes_t *pl, struct pkg_node *pkg_node) { bds_vector_append(pl, &pkg_node); }

void pkg_nodes_insert_sort(pkg_nodes_t *pkg_nodes, struct pkg_node *pkg_node)
{
        pkg_nodes_append(pkg_nodes, pkg_node);

        const struct pkg_node **pkgp_begin = (const struct pkg_node **)bds_vector_ptr(pkg_nodes);
        const struct pkg_node **pkgp       = pkgp_begin + bds_vector_size(pkg_nodes) - 1;

        while (pkgp != pkgp_begin) {
                if (pkg_nodes_compar(pkgp - 1, pkgp) <= 0) {
                        break;
                }

                const struct pkg_node *tmp = *(pkgp - 1);
                *(pkgp - 1)                = *pkgp;
                *pkgp                      = tmp;

                --pkgp;
        }
}

int pkg_nodes_remove(pkg_nodes_t *pkg_nodes, const char *pkg_name)
{
        struct pkg_node key;
        struct pkg_node *keyp = &key;

        key.pkg.name = (char *)pkg_name;

        struct pkg_node **pkgp = (struct pkg_node **)bds_vector_lsearch(pkg_nodes, &keyp, pkg_nodes_compar);
        if (pkgp == NULL)
                return 1; /* Nothing removed */

        size_t i = pkgp - (struct pkg_node **)bds_vector_ptr(pkg_nodes);
        bds_vector_remove(pkg_nodes, i);

        return 0;
}

void pkg_nodes_clear(pkg_nodes_t *pkg_nodes) { bds_vector_clear(pkg_nodes); }

struct pkg_node *pkg_nodes_lsearch(pkg_nodes_t *pkg_nodes, const char *name)
{
        return (struct pkg_node *)pkg_nodes_lsearch_const((const pkg_nodes_t *)pkg_nodes, name);
}

const struct pkg_node *pkg_nodes_lsearch_const(const pkg_nodes_t *pkg_nodes, const char *name)
{
        struct pkg_node key;
        const struct pkg_node *keyp = &key;

        key.pkg.name = (char *)name;

        const struct pkg_node **pkgp =
            (const struct pkg_node **)bds_vector_lsearch_const(pkg_nodes, &keyp, pkg_nodes_compar);
        if (pkgp)
                return *pkgp;

        return NULL;
}

struct pkg_node *pkg_nodes_bsearch(pkg_nodes_t *pkg_nodes, const char *name)
{
        return (struct pkg_node *)pkg_nodes_bsearch_const((const pkg_nodes_t *)pkg_nodes, name);
}

const struct pkg_node *pkg_nodes_bsearch_const(const pkg_nodes_t *pkg_nodes, const char *name)
{
        struct pkg_node key;
        const struct pkg_node *keyp = &key;

        key.pkg.name = (char *)name;

        const struct pkg_node **pkgp =
            (const struct pkg_node **)bds_vector_bsearch_const(pkg_nodes, &keyp, pkg_nodes_compar);
        if (pkgp)
                return *pkgp;

        return NULL;
}

struct pkg_graph *pkg_graph_alloc()
{
        struct pkg_graph *pkg_graph = calloc(1, sizeof(*pkg_graph));

        pkg_graph->sbo_pkgs  = pkg_nodes_alloc();
        pkg_graph->meta_pkgs = pkg_nodes_alloc();

        return pkg_graph;
}

struct pkg_graph *pkg_graph_alloc_reference()
{
        struct pkg_graph *pkg_graph = calloc(1, sizeof(*pkg_graph));

        pkg_graph->sbo_pkgs  = pkg_nodes_alloc_reference();
        pkg_graph->meta_pkgs = pkg_nodes_alloc_reference();

        return pkg_graph;
}

void pkg_graph_free(struct pkg_graph **pkg_graph)
{
        if (*pkg_graph == NULL)
                return;

        pkg_nodes_free(&(*pkg_graph)->sbo_pkgs);
        pkg_nodes_free(&(*pkg_graph)->meta_pkgs);

        free(*pkg_graph);
        *pkg_graph = NULL;
}

pkg_nodes_t *pkg_graph_sbo_pkgs(struct pkg_graph *pkg_graph) { return pkg_graph->sbo_pkgs; }

pkg_nodes_t *pkg_graph_assign_sbo_pkgs(struct pkg_graph *pkg_graph, pkg_nodes_t *sbo_pkgs)
{
        if (pkg_graph->sbo_pkgs)
                pkg_nodes_free(&pkg_graph->sbo_pkgs);
        pkg_graph->sbo_pkgs = sbo_pkgs;

        return pkg_graph->sbo_pkgs;
}

pkg_nodes_t *pkg_graph_meta_pkgs(struct pkg_graph *pkg_graph) { return pkg_graph->meta_pkgs; }

struct pkg_node *pkg_graph_search(struct pkg_graph *pkg_graph, const char *pkg_name)
{
        struct pkg_node *pkg_node = NULL;

        if ((pkg_node = pkg_nodes_bsearch(pkg_graph->sbo_pkgs, pkg_name)))
                return pkg_node;

        if ((pkg_node = pkg_nodes_bsearch(pkg_graph->meta_pkgs, pkg_name)))
                return pkg_node;

        if (is_meta_pkg(pkg_name)) {
                pkg_node                  = pkg_node_alloc(pkg_name);
                pkg_node->pkg.dep.is_meta = true;
                pkg_nodes_insert_sort(pkg_graph->meta_pkgs, pkg_node);
                // load_dep_file(pkg_graph, pkg_name, options);
        }

        return pkg_node;
}

static pkg_nodes_t *iterator_edge_nodes(const struct pkg_iterator *iter)
{
        struct pkg_node *cur_node = iter->cur_node;
        if (cur_node == NULL)
                return NULL;

        return (iter->flags & PKG_ITER_REVDEPS ? cur_node->pkg.dep.parents : cur_node->pkg.dep.required);
}

#define CUR_NODE_STATE(__iter_ptr) (pkg_node_state(__iter_ptr->cur_node, __iter_ptr->graph_id))
#define EDGE_NODE_STATE(__iter_ptr) (pkg_node_state(__iter_ptr->edge_node, __iter_ptr->graph_id))

static struct pkg_node *__iterator_next_forward(struct pkg_iterator *iter);
static struct pkg_node *__iterator_next_reverse(struct pkg_iterator *iter);

static size_t iterator_graph_id = 0;

struct pkg_node *pkg_iterator_begin(struct pkg_iterator *iter, struct pkg_graph *pkg_graph, const char *pkg_name,
                                    pkg_iterator_flags_t flags, int max_dist)
{
        memset(iter, 0, sizeof(*iter));

        struct pkg_node *node = pkg_graph_search(pkg_graph, pkg_name);
        if (node == NULL)
                return NULL;

        iter->flags       = flags;
        iter->max_dist    = max_dist;
        iter->visit_nodes = bds_stack_alloc(1, sizeof(struct pkg_node *), NULL);
        iter->graph_id    = iterator_graph_id++;

        pkg_graph_clear_markers(pkg_graph, iter->graph_id);

        if (iter->flags & PKG_ITER_FORW) {
                iter->next = __iterator_next_forward;

                iter->edge_node              = node;
                EDGE_NODE_STATE(iter)->color = COLOR_WHITE;
                EDGE_NODE_STATE(iter)->dist  = 0;

                return iter->edge_node;
        } else {
                iter->cur_node              = node;
                CUR_NODE_STATE(iter)->color = COLOR_GREY;
                CUR_NODE_STATE(iter)->dist  = 0;

                iter->next = __iterator_next_reverse;

                return iter->next(iter);
        }

        return NULL;
}

static int set_next_node_dist(struct pkg_iterator *iter)
{
        int incr = 1;
        if ((iter->flags & PKG_ITER_METAPKG_DIST) == 0) {
                incr = !(iter->flags & PKG_ITER_REVDEPS ? iter->edge_node->pkg.dep.is_meta
                                                        : iter->cur_node->pkg.dep.is_meta);
        }

        const int dist = CUR_NODE_STATE(iter)->dist + incr;

        if (EDGE_NODE_STATE(iter)->dist < 0) {
                EDGE_NODE_STATE(iter)->dist = dist;
        } else {
                /*
                  Take the minimum distance
                 */
                if (dist < EDGE_NODE_STATE(iter)->dist)
                        EDGE_NODE_STATE(iter)->dist = dist;
        }

        return EDGE_NODE_STATE(iter)->dist;
}

static struct pkg_node *get_next_edge_node(struct pkg_iterator *iter, pkg_nodes_t *edge_nodes)
{
        iter->edge_node = *(struct pkg_node **)bds_vector_get(edge_nodes, CUR_NODE_STATE(iter)->edge_index++);

        if (EDGE_NODE_STATE(iter)->color == COLOR_GREY) {
                fprintf(stderr, "cyclic dependency found: %s <--> %s\n", iter->cur_node->pkg.name,
                        iter->edge_node->pkg.name);
                exit(EXIT_FAILURE); // return NULL;
        }
        set_next_node_dist(iter);

        return iter->edge_node;
}

static struct pkg_node *__iterator_next_reverse(struct pkg_iterator *iter)
{
        if (iter->cur_node == NULL)
                return NULL;

        assert(CUR_NODE_STATE(iter)->color == COLOR_GREY);

        pkg_nodes_t *edge_nodes = iterator_edge_nodes(iter);

        const bool at_max_dist     = (iter->max_dist >= 0 && CUR_NODE_STATE(iter)->dist == iter->max_dist);
        const bool have_edge_nodes = (edge_nodes ? (CUR_NODE_STATE(iter)->edge_index >= 0 &&
                                                    CUR_NODE_STATE(iter)->edge_index < bds_vector_size(edge_nodes))
                                                 : false);

        if (!have_edge_nodes || at_max_dist) {
                if (at_max_dist && (iter->flags & PKG_ITER_REQ_NEAREST)) {
                        if (have_edge_nodes) {
                                if (get_next_edge_node(iter, edge_nodes) == NULL)
                                        return NULL;

                                EDGE_NODE_STATE(iter)->color = COLOR_BLACK;
                                return iter->edge_node;
                        }
                }

                assert(CUR_NODE_STATE(iter)->dist >= 0);

                CUR_NODE_STATE(iter)->color = COLOR_BLACK;
                iter->edge_node             = iter->cur_node;

                iter->cur_node = NULL;
                bds_stack_pop(iter->visit_nodes, &iter->cur_node);

                return iter->edge_node;
        }

        // Traverse to edge node
        while (CUR_NODE_STATE(iter)->edge_index < pkg_nodes_size(edge_nodes)) {

                if (get_next_edge_node(iter, edge_nodes) == NULL)
                        return NULL;

                if (EDGE_NODE_STATE(iter)->color == COLOR_BLACK && iter->flags & PKG_ITER_REQ_NEAREST) {
                        return iter->edge_node;
                }

                if (EDGE_NODE_STATE(iter)->color == COLOR_WHITE) {

                        // Visit the node
                        EDGE_NODE_STATE(iter)->color = COLOR_GREY;

                        bds_stack_push(iter->visit_nodes, &iter->cur_node);
                        iter->cur_node = iter->edge_node;

                        break;
                }
        }

        return __iterator_next_reverse(iter);
}

static struct pkg_node *__iterator_next_forward(struct pkg_iterator *iter)
{
        if (iter->cur_node == NULL && iter->edge_node == NULL)
                return NULL;

        if (iter->edge_node) {
                assert(EDGE_NODE_STATE(iter)->color != COLOR_GREY);
                if (EDGE_NODE_STATE(iter)->color == COLOR_WHITE) {
                        // Visit the node
                        EDGE_NODE_STATE(iter)->color = COLOR_GREY;

                        bds_stack_push(iter->visit_nodes, &iter->cur_node);
                        iter->cur_node = iter->edge_node;

                } else { /* COLOR_BLACK */

                        /*
                          If the edge node is black then the "at max distance" with the PKG_ITER_REQ_NEAREST flag
                          set was incountered upon previous entry.
                        */
                        assert(iter->flags & PKG_ITER_REQ_NEAREST);
                }
                iter->edge_node = NULL;
        }

        pkg_nodes_t *edge_nodes = iterator_edge_nodes(iter);

        const bool at_max_dist     = (iter->max_dist >= 0 && CUR_NODE_STATE(iter)->dist == iter->max_dist);
        const bool have_edge_nodes = (edge_nodes ? (CUR_NODE_STATE(iter)->edge_index >= 0 &&
                                                    CUR_NODE_STATE(iter)->edge_index < bds_vector_size(edge_nodes))
                                                 : false);

        if (!have_edge_nodes || at_max_dist) {
                if (at_max_dist && (iter->flags & PKG_ITER_REQ_NEAREST)) {
                        if (have_edge_nodes) {
                                if (get_next_edge_node(iter, edge_nodes) == NULL)
                                        return NULL;

                                EDGE_NODE_STATE(iter)->color = COLOR_BLACK;
                                return iter->edge_node;
                        }
                }

                assert(iter->edge_node == NULL);
                assert(CUR_NODE_STATE(iter)->dist >= 0);

                CUR_NODE_STATE(iter)->color = COLOR_BLACK;

                iter->cur_node = NULL;
                bds_stack_pop(iter->visit_nodes, &iter->cur_node);

                return __iterator_next_forward(iter);
        }

        // Traverse to edge node
        while (CUR_NODE_STATE(iter)->edge_index < pkg_nodes_size(edge_nodes)) {

                if (get_next_edge_node(iter, edge_nodes) == NULL)
                        return NULL;

                if (EDGE_NODE_STATE(iter)->color == COLOR_BLACK && iter->flags & PKG_ITER_REQ_NEAREST) {
                        return iter->edge_node;
                }

                if (EDGE_NODE_STATE(iter)->color == COLOR_WHITE)
                        return iter->edge_node;
        }

        /*
          The current (visit) node did not change. Mark the edge node as NULL so that it won't be visited upon
          reentry. Upon re-entry, the "not have edge nodes" rules will apply.
         */
        iter->edge_node = NULL;

        return __iterator_next_forward(iter);
}

struct pkg_node *pkg_iterator_next(struct pkg_iterator *iter) { return iter->next(iter); }

struct pkg_node *pkg_iterator_node(struct pkg_iterator *iter) { return iter->cur_node; }

int pkg_iterator_node_dist(const struct pkg_iterator *iter, struct pkg_node *node)
{
	return pkg_node_state_const(node, iter->graph_id)->dist;
}

void pkg_iterator_destroy(struct pkg_iterator *iter)
{
        if (iter->visit_nodes)
                bds_stack_free(&iter->visit_nodes);

        memset(iter, 0, sizeof(*iter));

	assert(iterator_graph_id > 0 );
	--iterator_graph_id;
}
