
/* Path handling. This was copied from Lua, with some minor adjustments.
   The biggest one is the lack of Windows support, because I don't have
   my Windows box setup for Lily yet (I'd rather make sure Windows works
   all at once). */

#define LILY_LIB_SUFFIX     ".so"

#define LILY_BASE_DIR       "/usr/local/"
#define LILY_SHARE_DIR      LILY_BASE_DIR "share/lily/" LILY_VERSION_DIR "/"
#define LILY_LIB_DIR        LILY_BASE_DIR "lib/lily/"   LILY_VERSION_DIR "/"

/* This is where Lily will attempt to import new files from, in addition to
   relative from the current import. */
#define LILY_PATH_SEED \
        LILY_SHARE_DIR ";" \
        LILY_LIB_DIR ";"

/* This controls what paths that Lily will attempt to load shared
   libraries from. */
#define LILY_LIBRARY_PATH_SEED \
        LILY_LIB_DIR ";"

#ifdef _WIN32
# define LILY_PATH_CHAR '\\'
#else
# define LILY_PATH_CHAR '/'
#endif
