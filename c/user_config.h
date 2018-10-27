#ifndef __USER_CONFIG_H__
#define __USER_CONFIG_H__


struct user_config {
	char *sbopkg_repo;
	char *depdir;
	char *sbo_tag;
	char *pager;
};

 
struct user_config default_user_config();
void destoy_user_config(struct user_config *user_config);
void load_user_config();

#endif
