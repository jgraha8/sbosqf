#ifndef __PKGLIST_H__
#define __PKGLIST_H__

#include <libbds/bds_vector.h>

typedef struct bds_vector pkg_stack_t;

struct pkg {
        char *name;
        int marked; // Used for graph analyses
};

struct pkg create_pkg(const char *name);
void destroy_pkg(struct pkg *pkg);

pkg_stack_t *load_pkglist(const char *pkgdb);
int write_pkglist(const pkg_stack_t *pkglist, const char *pkgdb);
void print_pkglist(const pkg_stack_t *pkglist);
struct pkg *find_pkg(pkg_stack_t *pkglist, const char *pkg_name);

int add_pkg(pkg_stack_t *pkglist, const char *pkgdb, const char *pkg_name);
int request_add_pkg(pkg_stack_t *pkglist, const char *pkgdb, const char *pkg_name);

int request_reviewed_add(const char *pkgdb, const char *pkg_name );
int request_review_pkg(const char *pkg_name);
int review_pkg(const char *pkg_name);

#endif // __PKGLIST_H__
