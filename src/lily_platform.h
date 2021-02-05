#ifndef LILY_PLATFORM_H
# define LILY_PLATFORM_H

# ifdef _WIN32
#  define LILY_PATH_CHAR '\\'
#  define LILY_PATH_SLASH "\\"
# else
#  define LILY_PATH_CHAR '/'
#  define LILY_PATH_SLASH "/"
# endif

# ifdef _WIN32
#  define LILY_LIB_SUFFIXES {"dll", NULL}
# elif __APPLE__
#  define LILY_LIB_SUFFIXES {"dylib", "so", NULL}
# else
#  define LILY_LIB_SUFFIXES {"so", NULL}
# endif
#endif
