#ifndef __CONFIG_H__
#define __CONFIG_H__

#define DEPDIR "/var/lib/sbosqf"
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
#define CONFIG ".sbosqf"
#define SBOPKG_REPO "/var/lib/sbopkg/compukix"
#define SLACKPKG_REPO_NAME "SLACKPKGPLUS_SBo"
#define SBO_TAG "_SBo"
#define PKGLIST "PKGLIST"
#define REVIEWED "REVIEWED"
#define PARENTDB "PARENTDB"
#define DEPDB "DEPDB"
#define OUTPUT_DIR "/var/lib/sbopkg/queues"
#define PROGRAM_NAME "sbosqf"

// gnulib config
#define _GL_INLINE         inline
#define _GL_EXTERN_INLINE  extern inline
#define _GL_ATTRIBUTE_PURE __attribute__((pure))

// Environment
#define PAGER "less -r"
#define EDITOR "vi"

#endif // __CONFIG_H__
