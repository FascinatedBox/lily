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

# define LILY_STRERROR_BUFFER_SIZE 128

# ifdef _WIN32
#  define lily_strerror(_buffer) \
        strerror_s(_buffer, sizeof(_buffer), errno)
# else
#  define lily_strerror(_buffer) \
        strerror_r(errno, _buffer, sizeof(_buffer))
# endif

/* LILY_CONFIG_SYS_DIRS_INIT defines the unprocessed default system dirs for
   import hooks to use. Since Windows does not specify a library directory, the
   executable location can be used instead. On Windows, LILY_DIR_PROCESS_CHAR is
   replaced by the location of the executable process.
   Non-Windows platforms are assumed to be *nix-based. For those, there are
   well-defined library directories.
   System path processing is done by `lily_ims_process_sys_dirs` */

# ifdef _WIN32
# define LILY_DIR_PROCESS_CHAR '|'
# define LILY_DIR_PROCESS_STR "|"

/* Follow the lead of %PATH%, which uses ';' as a separator. */
# define LILY_CONFIG_SYS_DIRS_INIT "|\\lib\\"
# define LILY_DIR_CHAR ';'
# define LILY_DIR_SEPARATOR ";"
# else

# define LILY_VERSION_DIR "lily/" LILY_MAJOR "." LILY_MINOR

/* Do what $PATH does and use ':' for the separator. */
# define LILY_CONFIG_SYS_DIRS_INIT \
        "/usr/local/lib/" LILY_VERSION_DIR "/" ":" \
        "/usr/lib/" LILY_VERSION_DIR "/"
# define LILY_DIR_CHAR ':'
# define LILY_DIR_SEPARATOR ":"
# endif

#endif
