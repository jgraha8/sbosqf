#ifndef __USER_CONFIG_H__
#define __USER_CONFIG_H__

#include "config.h"

struct user_config {
        char *sbopkg_repo;
        char *depdir;
        char *sbo_tag;
        char *pager;
	char *editor;
};

extern struct user_config user_config;

void init_user_config();
void destroy_user_config();
// void destoy_user_config(struct user_config *user_config);

#endif
