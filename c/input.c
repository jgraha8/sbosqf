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

int menu_display(int items, const char *mesg)
{
        struct bds_vector *actions = bds_vector_alloc(1, sizeof(struct menu_pair), NULL);

        if (mesg) {
                printf("%s\n", mesg);
        }
        printf("\nMenu options:\n\n");

        assert(items >= 0);

        struct menu_pair pair;
        if (items & MENU_REVIEW_PKG) {
                printf("\t(R)eview package\n\n");
                pair.key   = 'R';
                pair.value = MENU_REVIEW_PKG;
                bds_vector_append(actions, &pair);
        }

        if (items & MENU_ADD_PKG) {
                printf("\t(A)dd package to database\n\n");
                pair.key   = 'A';
                pair.value = MENU_ADD_PKG;
                bds_vector_append(actions, &pair);
        }

        if (items & MENU_ADD_REVIEWED) {
                printf("\tA(D)d package to REVIEWED\n\n");
                pair.key   = 'D';
                pair.value = MENU_ADD_REVIEWED;
                bds_vector_append(actions, &pair);
        }

        if (items & MENU_EDIT_DEP) {
                printf("\t(E)dit the dependency file\n\n");
                pair.key   = 'E';
                pair.value = MENU_EDIT_DEP;
                bds_vector_append(actions, &pair);
        }

        printf("\t(Q)uit\n\n");
        pair.key   = 'Q';
        pair.value = MENU_NONE;
        bds_vector_append(actions, &pair);

        printf("What do you want (");

        const struct menu_pair *p = (const struct menu_pair *)bds_vector_ptr(actions);
        for (size_t i = 0; i < bds_vector_size(actions); ++i) {
                if (i > 0) {
                        putchar('/');
                }
                putchar(p[i].key);
        }
        printf(")? ");

        struct menu_pair res = {.key = toupper(read_response())};

        p = bds_vector_lsearch(actions, &res, compar_pair);
        if (p == NULL) {
                bds_vector_free(&actions);
                return menu_display(items, mesg);
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
