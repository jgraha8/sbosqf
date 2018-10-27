#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <libbds/bds_string.h>

#include "filesystem.h"

const char *find_sbo_dir(const char *dir_name, const char *pkg_name)
{
        static char sbo_dir[4096];
        static int level = 0;

        const char *rp        = NULL;
        struct dirent *dirent = NULL;

        DIR *dp = opendir(dir_name);
        if (dp == NULL)
                return NULL;

        ++level;
	assert(level >= 1);

        if( level > 2 ) {
		goto finish;
	}

        while ((dirent = readdir(dp)) != NULL) {
		if( strcmp(dirent->d_name, "." ) == 0 ||
		    strcmp(dirent->d_name, ".." ) == 0  )
			continue;
		
                if (level == 1) {
                        if (dirent->d_type == DT_DIR) {
				char *next_dir = bds_string_dup_concat(3, dir_name, "/", dirent->d_name);
                                rp = find_sbo_dir(next_dir, pkg_name);
				free(next_dir);
                                if (rp) {
                                        goto finish;
                                }
                        }
                } else {
                        if (dirent->d_type == DT_DIR) {
                                if (strcmp(dirent->d_name, pkg_name) == 0) {
                                        rp = bds_string_copyf(sbo_dir, 4096, "%s/%s", dir_name, dirent->d_name);
                                        goto finish;
                                }
                        }
                }
        }

finish:
        if (dp)
                closedir(dp);
        --level;

        return rp;
}

const char *read_sbo_requires(const char *sbo_dir, const char *pkg_name)
{
	static char sbo_requires[4096];
	const char *rp = NULL;
	struct stat sb;	

	char *info_file = bds_string_dup_concat(4, sbo_dir, "/", pkg_name, ".info");     
	int fd = open(info_file, O_RDONLY);

	if( fd == -1 ) {
		perror("unable to read package info file");
		goto finish;
	}

	if( fstat(fd, &sb) != 0 ) {	
		perror("unable to stat package info file");
		goto finish;
	}

	char *info = (char *)mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if( info == NULL ) {
		perror("unable to map package info file");
		goto finish;
	}
	
	char *c = bds_string_find(info, "REQUIRES=");
	if( c == NULL ) {
		goto finish;
	}
	char *str = c + strlen("REQUIRES=");

	// Add terminator
	if( (c = bds_string_find(str, "\n")) ) {
		*c = '\0';
	}

	// Remove quotes
	c = str;
	while( *c ) {
		if( *c == '"')
			*c = ' ';
		++c;
	}
	strncpy(sbo_requires, bds_string_atrim(str), 4096);
	rp = sbo_requires;

finish:
	if( info_file )
		free(info_file);
	if( fd != -1 )
		close(fd);
	if( info )
		munmap(info, sb.st_size);

	return rp;
}
