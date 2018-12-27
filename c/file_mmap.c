#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "file_mmap.h"

struct file_mmap *file_mmap(const char *path)
{
        int rc               = 0;
        struct file_mmap *fm = NULL;
        struct stat sb;

        int fd = open(path, O_RDONLY);
        if (fd == -1) {
                perror("open()");
                rc = 1;
                goto finish;
        }

        if (fstat(fd, &sb) != 0) {
                perror("fstat()");
                rc = 1;
                goto finish;
        }

	const size_t data_len = sb.st_size + 1;
        char *data = (char *)mmap(NULL, data_len, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
        if (data == NULL) {
                perror("mmap()");
                rc = 1;
                goto finish;
        }
        data[data_len-1] = '\0';

finish:
        if (fd)
                close(fd);

        if (rc != 0) {
                if (data)
                        munmap(data, data_len);
        } else {
                assert((fm = malloc(sizeof(*fm))) != NULL);
                fm->data     = data;
                fm->data_len = data_len;
        }

        return fm;
}

int file_munmap(struct file_mmap **fm)
{
        int rc = 0;
        if (*fm == NULL)
                return 0;

        if (munmap((*fm)->data, (*fm)->data_len) == -1) {
                perror("munmap()");
                rc = 1;
                goto finish;
        }

finish:
        free(*fm);
        *fm = NULL;

        return rc;
}
