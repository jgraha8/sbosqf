#include <assert.h>
#include <ctype.h>
#include <stdio.h>

#include <libbds/bds_string.h>
#include <libbds/bds_vector.h>

#include "input.h"

struct menu_pair {
        char key;
        int value;
};

static int compar_pair(const void *a, const void *b)
{
        return (((const struct menu_pair *)a)->key - ((const struct menu_pair *)b)->key);
}

int menu_display(int items, const char *title, const char *msg)
{
        struct bds_vector *actions = bds_vector_alloc(1, sizeof(struct menu_pair), NULL);

        bool add_newline = false;
        if (title) {
                printf("%s\n", title);
                add_newline = true;
        }
        if (msg) {
                if (add_newline)
                        printf("\n");

                printf("%s\n", msg);
                add_newline = true;
        }
        if (add_newline)
                printf("\n");

        printf("Options:\n\n");

        assert(items >= 0);

        int key = 0;

        struct menu_pair pair;
        if (items & MENU_CREATE_DEP) {
                pair.key   = ++key;
                pair.value = MENU_CREATE_DEP;
                bds_vector_append(actions, &pair);
                printf("\t%d) Create default dependency file\n\n", pair.key);
        }

        if (items & MENU_REVIEW_PKG) {
                pair.key   = ++key;
                pair.value = MENU_REVIEW_PKG;
                bds_vector_append(actions, &pair);
                printf("\t%d) Review package\n\n", pair.key);
        }
        if (items & MENU_EDIT_DEP) {
                pair.key   = ++key;
                pair.value = MENU_EDIT_DEP;
                bds_vector_append(actions, &pair);
                printf("\t%d) Edit dependency file\n\n", pair.key);
        }
        if (items & MENU_DELETE_DEP) {
                pair.key   = ++key;
                pair.value = MENU_DELETE_DEP;
                bds_vector_append(actions, &pair);
                printf("\t%d) Delete dependency file\n\n", pair.key);
        }
        if (items & MENU_ADD_PKG) {
                pair.key   = ++key;
                pair.value = MENU_ADD_PKG;
                bds_vector_append(actions, &pair);
                printf("\t%d) Add package to PKGLIST\n\n", pair.key);
        }
        if (items & MENU_REMOVE_PKG) {
                pair.key   = ++key;
                pair.value = MENU_REMOVE_PKG;
                bds_vector_append(actions, &pair);
                printf("\t%d) Remove package from PKGLIST\n\n", pair.key);
        }
        if (items & MENU_ADD_REVIEWED) {
                pair.key   = ++key;
                pair.value = MENU_ADD_REVIEWED;
                bds_vector_append(actions, &pair);
                printf("\t%d) Add package to REVIEWED\n\n", pair.key);
        }
        if (items & MENU_REMOVE_REVIEWED) {
                pair.key   = ++key;
                pair.value = MENU_REMOVE_REVIEWED;
                bds_vector_append(actions, &pair);
                printf("\t%d) Remove package from REVIEWED\n\n", pair.key);
        }

        pair.key   = ++key;
        pair.value = MENU_NONE;
        bds_vector_append(actions, &pair);
        printf("\t%d) Quit\n\n", pair.key);

        printf("What do you want (1-%d)? ", key);

        /* const struct menu_pair *p = (const struct menu_pair *)bds_vector_ptr(actions); */
        /* for (size_t i = 0; i < bds_vector_size(actions); ++i) { */
        /*         if (i > 0) { */
        /*                 putchar('/'); */
        /*         } */
        /*         putchar(p[i].key); */
        /* } */
        /* printf(")? "); */

        struct menu_pair res = {.key = read_response() - '0'};

        const struct menu_pair *p = bds_vector_lsearch(actions, &res, compar_pair);
        if (p == NULL) {
                bds_vector_free(&actions);
                return menu_display(items, title, msg);
        }

        int rc = p->value;
        bds_vector_free(&actions);

        return rc;
}

char read_response()
{
        char response[4096] = {0};

        if (fgets(response, sizeof(response), stdin) == NULL) {
                return -1;
        }

        char *c;

        // Expect newline
        if ((c = bds_string_rfind(response, "\n"))) {
                *c = '\0';
        } else {
                return -1;
        }

        // Expect only one character
        if (response[1])
                return -1;

        return response[0];
}
