#ifdef _WIN32
# define LILY_PATH_CHAR '\\'
# define LILY_PATH_SLASH "\\"
# define LILY_LIB_SUFFIX "dll"
#else
# define LILY_PATH_CHAR '/'
# define LILY_PATH_SLASH "/"
# define LILY_LIB_SUFFIX "so"
#endif
