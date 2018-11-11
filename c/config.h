#ifndef __CONFIG_H__
#define __CONFIG_H__

#define DEPDIR "/var/lib/sbopkg-dep2sqf"
#define PKGFILE

#define BLACK "01;30"
#define RED "01;31"
#define GREEN "01;32"
#define YELLOW "01;33"
#define BLUE '01;34'
#define MAGENTA "01;35"
#define CYAN "01;36"

#define COLOR_OK "\033[${GREEN}m"
#define COLOR_INFO "\033[${YELLOW}m"
#define COLOR_WARN "\033[${MAGENTA}m"
#define COLOR_FAIL "\033[${RED}m"
#define COLOR_END "\033[0m"

// Parameters
#define CONFIG ".sbopkg-dep2sqf"
#define SBOPKG_REPO "/var/lib/sbopkg/SBo"
#define SBO_TAG "_SBo"
#define PKGLIST "PKGLIST"
#define REVIEWED "REVIEWED"
#define PARENTDB "PARENTDB"
#define DEPDB "DEPDB"

// Environment
#define PAGER "less -r"
#define EDITOR "vi"

#endif // __CONFIG_H__
