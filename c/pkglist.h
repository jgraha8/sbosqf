#ifndef __PKGLIST_H__
#define __PKGLIST_H__

#include <libbds/bds_stack.h>

struct pkg {
	char *name;
	int marked; // Used for graph analyses
};

struct pkg create_pkg(const char *name);
void destroy_pkg(struct pkg *pkg);

struct bds_stack *load_pkglist(const char *depdir);
void print_pkglist(const struct bds_stack *pkglist);
struct pkg *find_pkg(struct bds_stack *pkglist, const char *pkg_name);
	
#endif // __PKGLIST_H__
