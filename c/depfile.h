#ifndef __DEPFILE_H__
#define __DEPFILE_H__

#include <stdbool.h>
#include <libbds/bds_stack.h>

struct dep {
	char *pkg_name;
	struct bds_stack *required;
	struct bds_stack *optional;
	struct bds_stack *buildopts;
	bool is_meta;
};

struct dep_list {
	char *pkg_name;
	struct bds_stack *dep_list;
};

struct pkg_parents {
	char *pkg_name;
	struct bds_stack *parents;
};

enum block_type { NO_BLOCK, REQUIRED_BLOCK, OPTIONAL_BLOCK, BUILDOPTS_BLOCK };

struct dep *dep_alloc(const char *pkg_name);
void dep_free(struct dep **dep);

struct dep_list *dep_list_alloc(const char *pkg_name);
void dep_list_free(struct dep_list **dep_list);

struct dep *load_depfile(const char *depdir, const char *pkg_name, bool recursive);

void print_depfile(const struct dep *dep);

void write_deplist(FILE *fp, const struct dep *dep );
int write_depdb(const char *depdir, const struct bds_stack *pkglist);


#endif // __DEPFILE_H__
