#include <libbds/bds_stack.h>

struct bds_stack *load_pkglist(const char *depdir);
void print_pkglist(const struct bds_stack *pkglist);
