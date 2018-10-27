#ifndef __PKGLIST_H__
#define __PKGLIST_H__

#include <libbds/bds_stack.h>

typedef struct bds_stack pkg_stack_t;

struct pkg {
	char *name;
	int marked; // Used for graph analyses
};

struct pkg create_pkg(const char *name);
void destroy_pkg(struct pkg *pkg);

pkg_stack_t *load_pkglist(const char *depdir);
void print_pkglist(const pkg_stack_t *pkglist);
struct pkg *find_pkg(pkg_stack_t *pkglist, const char *pkg_name);
	
#endif // __PKGLIST_H__
