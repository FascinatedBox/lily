
/* Path handling. This was copied from Lua, with some minor adjustments.
   The biggest one is the lack of Windows support, because I don't have
   my Windows box setup for Lily yet (I'd rather make sure Windows works
   all at once). */
#define LILY_MAJOR "0"
#define LILY_MINOR "13"
#define LILY_VERSION_DIR   LILY_MAJOR "." LILY_MINOR

#define LILY_BASE_DIR	"/usr/local/"
#define LILY_SHARE_DIR	LILY_ROOT "share/lua/" LUA_VDIR "/"
#define LILY_CDIR	LILY_ROOT "lib/lua/" LUA_VDIR "/"

/* So, by default, Lily will attempt to load .lly files from these
   directories. */
#define LILY_PATH_SEED \
        LILY_BASE_DIR "share/" LILY_VERSION_DIR "/;" \
        LILY_BASE_DIR "lib/" LILY_VERSION_DIR "/;" \
        "./;"
