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

        pkg_node->pkg = pkg_create(name);

        return pkg_node;
}

void pkg_node_destroy(struct pkg_node *pkg_node)
{
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

void pkg_node_clear_markers(struct pkg_node *pkg_node)
{
        pkg_node->dist       = -1;
        pkg_node->color      = COLOR_WHITE;
        pkg_node->edge_index = 0;
}

void pkg_graph_clear_markers(struct pkg_graph *pkg_graph)
{
        size_t n = 0;

        n = bds_vector_size(pkg_graph->sbo_pkgs);
        for (size_t i = 0; i < n; ++i) {
                struct pkg_node *pkg_node = *(struct pkg_node **)bds_vector_get(pkg_graph->sbo_pkgs, i);
                pkg_node_clear_markers(pkg_node);
        }

        n = bds_vector_size(pkg_graph->meta_pkgs);
        for (size_t i = 0; i < n; ++i) {
                struct pkg_node *pkg_node = *(struct pkg_node **)bds_vector_get(pkg_graph->meta_pkgs, i);
                pkg_node_clear_markers(pkg_node);
        }
}

pkg_nodes_t *pkg_nodes_alloc_reference() { return bds_vector_alloc(1, sizeof(struct pkg_node *), NULL); }
pkg_nodes_t *pkg_nodes_alloc()
{
        return bds_vector_alloc(1, sizeof(struct pkg_node *), (void (*)(void *))pkg_node_free);
}

void pkg_nodes_free(pkg_nodes_t **pl) { bds_vector_free(pl); }

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

int pkg_nodes_remove(pkg_nodes_t *pl, const char *pkg_name)
{
        struct pkg_node key;
        struct pkg_node *keyp = &key;

        key.pkg.name = (char *)pkg_name;

        struct pkg_node **pkgp = (struct pkg_node **)bds_vector_lsearch(pl, &keyp, pkg_nodes_compar);
        if (pkgp == NULL)
                return 1;

        pkg_node_destroy(*pkgp);
        bds_vector_qsort(pl, pkg_nodes_compar);

        return 0;
}

struct pkg_node *pkg_nodes_lsearch(pkg_nodes_t *pl, const char *name)
{
        return (struct pkg_node *)pkg_nodes_lsearch_const((const pkg_nodes_t *)pl, name);
}

const struct pkg_node *pkg_nodes_lsearch_const(const pkg_nodes_t *pl, const char *name)
{
        struct pkg_node key;
        const struct pkg_node *keyp = &key;

        key.pkg.name = (char *)name;

        const struct pkg_node **pkgp = (const struct pkg_node **)bds_vector_lsearch_const(pl, &keyp, pkg_nodes_compar);
        if (pkgp)
                return *pkgp;

        return NULL;
}

struct pkg_node *pkg_nodes_bsearch(pkg_nodes_t *pl, const char *name)
{
        return (struct pkg_node *)pkg_nodes_bsearch_const((const pkg_nodes_t *)pl, name);
}

const struct pkg_node *pkg_nodes_bsearch_const(const pkg_nodes_t *pl, const char *name)
{
        struct pkg_node key;
        const struct pkg_node *keyp = &key;

        key.pkg.name = (char *)name;

        const struct pkg_node **pkgp = (const struct pkg_node **)bds_vector_bsearch_const(pl, &keyp, pkg_nodes_compar);
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

static struct pkg_node *__iterator_next_forward(struct pkg_iterator *iter);
static struct pkg_node *__iterator_next_reverse(struct pkg_iterator *iter);

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

        pkg_graph_clear_markers(pkg_graph);

        if (iter->flags & PKG_ITER_FORW) {
                iter->next = __iterator_next_forward;

                iter->edge_node        = node;
                iter->edge_node->color = COLOR_WHITE;
                iter->edge_node->dist  = 0;

                return iter->edge_node;
        } else {
                iter->cur_node        = node;
                iter->cur_node->color = COLOR_GREY;
                iter->cur_node->dist  = 0;

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

        const int dist = iter->cur_node->dist + incr;

        if (iter->edge_node->dist < 0) {
                iter->edge_node->dist = dist;
        } else {
                /*
                  Take the minimum distance
                 */
                if (dist < iter->edge_node->dist)
                        iter->edge_node->dist = dist;
        }

        return iter->edge_node->dist;
}

static struct pkg_node *get_next_edge_node(struct pkg_iterator *iter, pkg_nodes_t *edge_nodes)
{
        iter->edge_node = *(struct pkg_node **)bds_vector_get(edge_nodes, iter->cur_node->edge_index++);

        if (iter->edge_node->color == COLOR_GREY) {
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

        assert(iter->cur_node->color == COLOR_GREY);

        pkg_nodes_t *edge_nodes = iterator_edge_nodes(iter);

        const bool at_max_dist     = (iter->max_dist >= 0 && iter->cur_node->dist == iter->max_dist);
        const bool have_edge_nodes = (edge_nodes ? (iter->cur_node->edge_index >= 0 &&
                                                    iter->cur_node->edge_index < bds_vector_size(edge_nodes))
                                                 : false);

        if (!have_edge_nodes || at_max_dist) {
                if (at_max_dist && (iter->flags & PKG_ITER_REQ_NEAREST)) {
                        if (have_edge_nodes) {
                                if (get_next_edge_node(iter, edge_nodes) == NULL)
                                        return NULL;

                                iter->edge_node->color = COLOR_BLACK;
                                return iter->edge_node;
                        }
                }

                assert(iter->cur_node->dist >= 0);

                iter->cur_node->color = COLOR_BLACK;
                iter->edge_node       = iter->cur_node;

                iter->cur_node = NULL;
                bds_stack_pop(iter->visit_nodes, &iter->cur_node);

                return iter->edge_node;
        }

        // Traverse to edge node
        while (iter->cur_node->edge_index < pkg_nodes_size(edge_nodes)) {

                if (get_next_edge_node(iter, edge_nodes) == NULL)
                        return NULL;

                if (iter->edge_node->color == COLOR_BLACK && iter->flags & PKG_ITER_REQ_NEAREST) {
                        return iter->edge_node;
                }

                if (iter->edge_node->color == COLOR_WHITE) {

                        // Visit the node
                        iter->edge_node->color = COLOR_GREY;

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
                assert(iter->edge_node->color != COLOR_GREY);
                if (iter->edge_node->color == COLOR_WHITE) {
                        // Visit the node
                        iter->edge_node->color = COLOR_GREY;

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

        const bool at_max_dist     = (iter->max_dist >= 0 && iter->cur_node->dist == iter->max_dist);
        const bool have_edge_nodes = (edge_nodes ? (iter->cur_node->edge_index >= 0 &&
                                                    iter->cur_node->edge_index < bds_vector_size(edge_nodes))
                                                 : false);

        if (!have_edge_nodes || at_max_dist) {
                if (at_max_dist && (iter->flags & PKG_ITER_REQ_NEAREST)) {
                        if (have_edge_nodes) {
                                if (get_next_edge_node(iter, edge_nodes) == NULL)
                                        return NULL;

                                iter->edge_node->color = COLOR_BLACK;
                                return iter->edge_node;
                        }
                }

                assert(iter->edge_node == NULL);
                assert(iter->cur_node->dist >= 0);

                iter->cur_node->color = COLOR_BLACK;

                iter->cur_node = NULL;
                bds_stack_pop(iter->visit_nodes, &iter->cur_node);

                return __iterator_next_forward(iter);
        }

        // Traverse to edge node
        while (iter->cur_node->edge_index < pkg_nodes_size(edge_nodes)) {

                if (get_next_edge_node(iter, edge_nodes) == NULL)
                        return NULL;

                if (iter->edge_node->color == COLOR_BLACK && iter->flags & PKG_ITER_REQ_NEAREST) {
                        return iter->edge_node;
                }

                if (iter->edge_node->color == COLOR_WHITE)
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

void pkg_iterator_destroy(struct pkg_iterator *iter)
{
        if (iter->visit_nodes)
                bds_stack_free(&iter->visit_nodes);

        memset(iter, 0, sizeof(*iter));
}
