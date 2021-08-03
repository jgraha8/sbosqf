#ifndef __USER_CONFIG_H__
#define __USER_CONFIG_H__

#include "config.h"

struct user_config {
        char *sbopkg_repo;
	char *slackpkg_repo_name;
        char *depdir;
        char *sbo_tag;
        char *pager;
	char *editor;
        char *output_dir;
};

extern struct user_config user_config;

void user_config_init();
void user_config_destroy();
// void destoy_user_config(struct user_config *user_config);

#endif
