/*
 * Copyright (C) 2018-2019 Jason Graham <jgraham@compukix.net>
 *

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 *
 */

#ifndef __PKG_H__
#define __PKG_H__

#include <stdint.h>

#include <zlib.h>

#include <libbds/bds_string.h>
#include <libbds/bds_vector.h>

#define PKG_CHECK_INSTALLED 0x1
#define PKG_CHECK_ANY_INSTALLED 0x2
// #define PKG_AUTO_REVIEW_ADD 0x1
//#define PKG_AUTO_REVIEW_QUIET 0x2

#define PKG_DEP_REVERTED_DEFAULT 0x1
#define PKG_DEP_EDITED 0x2

typedef struct bds_vector pkg_nodes_t;
typedef struct bds_vector buildopts_t;

struct pkg;
struct pkg_node;

struct pkg_dep {
        pkg_nodes_t *required;
        pkg_nodes_t *parents;
        buildopts_t *buildopts;
        bool is_meta;
};

struct pkg {
        char *name;
        char *version;
        char *sbo_dir;
        struct pkg_dep dep;
        uint32_t info_crc; /// CRC of README and REQUIRED list in .info
                           // struct pkg_sbo *sbo;
        bool parent_installed;
        bool for_removal;
};

struct pkg pkg_create(const char *name);
struct pkg *pkg_dup_nodep(const struct pkg *pkg_src);
void pkg_destroy(struct pkg *pkg);

void pkg_copy_nodep(struct pkg *pkg_dst, const struct pkg *pkg_src);
void pkg_init_version(struct pkg *pkg, const char *version);
void pkg_set_version(struct pkg *pkg, const char *version);
void pkg_init_sbo_dir(struct pkg *pkg, const char *sbo_dir);
int pkg_set_info_crc(struct pkg *pkg);
void pkg_insert_required(struct pkg *pkg, struct pkg_node *req_node);
void pkg_remove_required(struct pkg *pkg, struct pkg_node *req_node);
/**
 * @brief Clears all required package nodes from the package dependencies
 *
 * The required package nodes will also have the specified package
 * removed from their parent nodes list.
 */
void pkg_clear_required(struct pkg *pkg);
struct pkg *pkg_bsearch_required(struct pkg *pkg, const char *req_name);

void pkg_insert_parent(struct pkg *pkg, struct pkg_node *parent_node);
void pkg_remove_parent(struct pkg *pkg, struct pkg_node *parent_node);
struct pkg *pkg_bsearch_parent(struct pkg *pkg, const char *parent_name);

void pkg_append_buildopts(struct pkg *pkg, char *bopt);

size_t pkg_buildopts_size(const struct pkg *pkg);
const char *pkg_buildopts_get_const(const struct pkg *pkg, size_t i);

enum pkg_review_type { PKG_REVIEW_DISABLED = 0, PKG_REVIEW_ENABLED, PKG_REVIEW_AUTO, PKG_REVIEW_AUTO_VERBOSE };

enum pkg_output_mode { PKG_OUTPUT_FILE = 0, PKG_OUTPUT_STDOUT };


struct pkg_options {
        int check_installed;              /* Bit flags for checking packages are already installed */
        enum pkg_review_type review_type; /* Selection for how packages are reviewed */
	enum pkg_output_mode output_mode; /* Output mode selection */
        bool recursive;                   /* Recursive dep file parsing */
        bool optional;                    /* Include optional packages in dep file parsing */
        bool revdeps;                     /* Include reverse dependencies */
        bool deep;                        /* Perform deep graph processing */
        bool graph;
};

struct pkg_options pkg_options_default();

// pkg_nodes_t *pkg_load_sbo();

#endif
