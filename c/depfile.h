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

enum block_type { NO_BLOCK, REQUIRED_BLOCK, OPTIONAL_BLOCK, BUILDOPTS_BLOCK };

struct dep *dep_alloc(const char *pkg_name);
void dep_free(struct dep **dep);
struct dep *load_depfile(const char *depdir, const char *pkg_name);

void print_depfile(const struct dep *dep);

#endif // __DEPFILE_H__
