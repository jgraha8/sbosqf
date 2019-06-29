#ifndef __CONFIG_H__
#define __CONFIG_H__

#define DEPDIR "/var/lib/sbopkg-dep2sqf"
#define PKGFILE

#define RED "\x1B[31m"
#define GREEN "\x1B[32m"
#define YELLOW "\x1B[33m"
#define BLUE "\x1B[34m"
#define MAGENTA "\x1B[35m"
#define CYAN "\x1B[36m"
#define WHITE "\x1B[37m"

#define COLOR_OK GREEN
#define COLOR_INFO YELLOW
#define COLOR_WARN MAGENTA
#define COLOR_FAIL RED
#define COLOR_END "\x1B[0m"

// Parameters
#define CONFIG ".sbopkg-dep2sqf"
#define SBOPKG_REPO "/var/lib/sbopkg/compukix"
#define SBO_TAG "_SBo"
#define PKGLIST "PKGLIST"
#define REVIEWED "REVIEWED"
#define PARENTDB "PARENTDB"
#define DEPDB "DEPDB"
#define PROGRAM_NAME "sbopkg-dep2sqf"

// gnulib config
#define _GL_INLINE         inline
#define _GL_EXTERN_INLINE  extern inline
#define _GL_ATTRIBUTE_PURE __attribute__((pure))

// Environment
#define PAGER "more"
#define EDITOR "vi"

#endif // __CONFIG_H__
