#ifndef __DEPFILE_H__
#define __DEPFILE_H__

#include <stdbool.h>
#include <libbds/bds_stack.h>

#include "pkglist.h"

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

typedef struct bds_stack dep_parents_stack_t;

struct dep_parents {
	char *pkg_name;
	dep_stack_t *parents_list;
};


enum block_type { NO_BLOCK, REQUIRED_BLOCK, OPTIONAL_BLOCK, BUILDOPTS_BLOCK };

struct dep *dep_alloc(const char *pkg_name);
void dep_free(struct dep **dep);

struct dep_list *dep_list_alloc(const char *pkg_name);
void dep_list_free(struct dep_list **dep_list);

struct dep_parents *dep_parents_alloc(const char *pkg_name);
void dep_parents_free(struct dep_parents **dp);
dep_parents_stack_t *dep_parents_stack_alloc();
void dep_parents_stack_free(dep_parents_stack_t **dps);
struct dep_parents *dep_parents_stack_search(dep_parents_stack_t *dps, const char *pkg_name);

struct dep *load_depfile(const char *depdir, const char *pkg_name, bool recursive);

void print_dep_sqf(const struct dep *dep);

void write_deplist(FILE *fp, const struct dep *dep );
int write_depdb(const char *depdir, const pkg_stack_t *pkglist);

struct dep_list *load_dep_list(const char *depdir, const char *pkg_name, bool recursive);
struct dep_list *load_dep_list_from_dep(const struct dep *dep);


int write_parentdb(const char *depdir, const pkg_stack_t *pkglist);



#endif // __DEPFILE_H__
