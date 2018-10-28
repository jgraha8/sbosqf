#ifndef __DEPS_H__
#define __DEPS_H__

#include <stdbool.h>
#include <libbds/bds_stack.h>

#include "pkglist.h"

typedef struct bds_stack string_stack_t;
typedef struct bds_stack dep_stack_t;
typedef struct bds_stack dep_info_stack_t;

struct dep_info {
	char *pkg_name;
	string_stack_t *buildopts;
	bool is_meta;
};

struct dep {
	struct dep_info info;
	dep_stack_t *required;
	dep_stack_t *optional;
};

struct dep_list {
	struct dep_info info;
	dep_info_stack_t *dep_list;
};

typedef struct bds_stack dep_parents_stack_t;

struct dep_parents {
	struct dep_info info;
	dep_info_stack_t *parents_list;
};

enum block_type { NO_BLOCK, REQUIRED_BLOCK, OPTIONAL_BLOCK, BUILDOPTS_BLOCK };

struct dep_info dep_info_ctor(const char *pkg_name);
void dep_info_dtor(struct dep_info *dep_info);

struct dep_info dep_info_deep_copy(const struct dep_info *src);

struct dep *dep_alloc(const char *pkg_name);
void dep_free(struct dep **dep);

struct dep_list *dep_list_alloc(const char *pkg_name);
struct dep_list *dep_list_alloc_with_info(const struct dep_info *info);
void dep_list_free(struct dep_list **dep_list);

struct dep_parents *dep_parents_alloc(const char *pkg_name);
void dep_parents_free(struct dep_parents **dp);
dep_parents_stack_t *dep_parents_stack_alloc();
void dep_parents_stack_free(dep_parents_stack_t **dps);
struct dep_parents *dep_parents_stack_search(dep_parents_stack_t *dps, const char *pkg_name);

struct dep *load_dep_file(const char *depdir, const char *pkg_name, bool recursive, bool optional);

void print_dep_sqf(const struct dep *dep);

void write_deplist(FILE *fp, const struct dep *dep );
int write_depdb(const char *depdir, const pkg_stack_t *pkglist, bool recursive, bool optional);

struct dep_list *load_dep_list(const char *depdir, const char *pkg_name, bool recursive, bool optional);
struct dep_list *load_dep_list_from_dep(const struct dep *dep);


int write_parentdb(const char *depdir, const pkg_stack_t *pkglist, bool recursive, bool optional);



#endif // __DEPS_H__
