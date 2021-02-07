#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
# include <direct.h>
#else
# include <sys/stat.h>
# include <unistd.h>
#endif

#include "lily.h"
#include "lily_platform.h"
#define LILY_NO_EXPORT
#include "lily_pkg_fs_bindings.h"

extern void lily_mb_reserve(lily_msgbuf *, uint32_t);
extern int lily_mb_size(lily_msgbuf *);

#define HANDLE_RESULT \
if (result == -1) { \
    char buffer[LILY_STRERROR_BUFFER_SIZE]; \
 \
    lily_strerror(buffer); \
    lily_IOError(s, "Errno %d: %s (%s).", errno, buffer, dirname_raw); \
} \

void lily_fs__change_dir(lily_state *s)
{
    char *dirname_raw = lily_arg_string_raw(s, 0);
    int result;

    errno = 0;

#ifdef _WIN32
    result = _chdir(dirname_raw);
#else
    result = chdir(dirname_raw);
#endif

    HANDLE_RESULT
    lily_return_unit(s);
}

void lily_fs__create_dir(lily_state *s)
{
    char *dirname_raw = lily_arg_string_raw(s, 0);
    int result;

#ifndef _WIN32
    int mode = 0777;

    if (lily_arg_count(s) == 2)
        mode = (int)lily_arg_integer(s, 1);
#endif

    errno = 0;

#ifdef _WIN32
    result = _mkdir(dirname_raw);
#else
    result = mkdir(dirname_raw, mode);
#endif

    HANDLE_RESULT
    lily_return_unit(s);
}

void lily_fs__current_dir(lily_state *s)
{
    lily_msgbuf *vm_buffer = lily_msgbuf_get(s);
    uint32_t size = lily_mb_size(vm_buffer);
    char *buffer, *result;

    while (1) {
        buffer = (char *)lily_mb_raw(vm_buffer);

#ifdef _WIN32
        result = _getcwd(buffer, size);
#else
        result = getcwd(buffer, size);
#endif

        if (result)
            break;

        size *= 2;
        lily_mb_reserve(vm_buffer, size);
    }

    lily_return_string(s, buffer);
}

void lily_fs__remove_dir(lily_state *s)
{
    char *dirname_raw = lily_arg_string_raw(s, 0);
    int result;

    errno = 0;

#ifdef _WIN32
    result = _rmdir(dirname_raw);
#else
    result = rmdir(dirname_raw);
#endif

    HANDLE_RESULT
    lily_return_unit(s);
}

LILY_DECLARE_FS_CALL_TABLE
