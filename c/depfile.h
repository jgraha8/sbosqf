#ifndef __DEPFILE_H__
#define __DEPFILE_H__

#include <stdbool.h>
#include <libbds/bds_stack.h>

typedef struct bds_stack dep_stack_t;

struct dep {
	char *pkg_name;
	dep_stack_t *required;
	dep_stack_t *optional;
	dep_stack_t *buildopts;
	bool is_meta;
};

struct dep_list {
	char *pkg_name;
	dep_stack_t *dep_list;
};

/* typedef struct bds_stack pkg_stack_t; */

/* struct pkg_parents { */
/* 	char *pkg_name; */
/* 	struct bds_stack *parents; */
/* }; */

enum block_type { NO_BLOCK, REQUIRED_BLOCK, OPTIONAL_BLOCK, BUILDOPTS_BLOCK };

struct dep *dep_alloc(const char *pkg_name);
void dep_free(struct dep **dep);

struct dep_list *dep_list_alloc(const char *pkg_name);
void dep_list_free(struct dep_list **dep_list);

struct dep *load_depfile(const char *depdir, const char *pkg_name, bool recursive);

void print_dep_sqf(const struct dep *dep);

void write_deplist(FILE *fp, const struct dep *dep );
int write_depdb(const char *depdir, const struct bds_stack *pkglist);

struct dep_list *load_dep_list(const char *depdir, const char *pkg_name);
struct dep_list *load_dep_list_from_dep(const struct dep *dep);


#endif // __DEPFILE_H__
