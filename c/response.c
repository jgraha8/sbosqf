#include <libbds/bds_string.h>
#include <stdio.h>

#include "response.h"

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
