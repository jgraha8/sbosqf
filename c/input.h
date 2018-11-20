#ifndef __INPUT_H__
#define __INPUT_H__

#include <stdbool.h>

#define MENU_ERROR        -1
#define MENU_NONE         0x00
#define MENU_REVIEW_PKG   0x01
#define MENU_ADD_PKG      0x02
#define MENU_ADD_REVIEWED 0x04
#define MENU_EDIT_DEP     0x08
#define MENU_ALL          0xFF

int menu_display(int items, const char *mesg);
char read_response();

#endif
