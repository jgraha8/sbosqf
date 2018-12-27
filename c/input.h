#ifndef __INPUT_H__
#define __INPUT_H__

#include <stdbool.h>

#define MENU_ERROR           -1
#define MENU_NONE            0
#define MENU_CREATE_DEP      (1<<0)
#define MENU_REVIEW_PKG      (1<<1)
#define MENU_EDIT_DEP        (1<<2)
#define MENU_REMOVE_DEP      (1<<3)
#define MENU_ADD_PKG         (1<<4)
#define MENU_REMOVE_PKG      (1<<5)
#define MENU_ADD_REVIEWED    (1<<6)
#define MENU_REMOVE_REVIEWED (1<<7)
#define MENU_ALL             0xFFFF

int menu_display(int items, const char *mesg);
char read_response();

#endif
