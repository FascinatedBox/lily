#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
# undef UNICODE
# include <direct.h>
# include <windows.h>
#else
# include <dirent.h>
# include <sys/stat.h>
# include <unistd.h>
#endif

#include "lily.h"
#include "lily_platform.h"
#define LILY_NO_EXPORT
#include "lily_pkg_fs_bindings.h"

extern void lily_mb_reserve(lily_msgbuf *, uint32_t);
extern int lily_mb_size(lily_msgbuf *);

typedef struct {
    LILY_FOREIGN_HEADER
    int visited;

#ifdef _WIN32
    HANDLE cursor;
#else
    DIR *cursor;
#endif
} lily_fs_Dir;

#define HANDLE_RESULT \
if (result == -1) { \
    char buffer[LILY_STRERROR_BUFFER_SIZE]; \
 \
    lily_strerror(buffer); \
    lily_IOError(s, "Errno %d: %s (%s).", errno, buffer, dirname_raw); \
} \

void lily_fs_destroy_Dir(lily_fs_Dir *d)
{
    if (d->cursor)
#ifdef _WIN32
        FindClose(d->cursor);
#else
        closedir(d->cursor);
#endif
}

static int is_dot_or_dot_dot(const char *path)
{
    if (*path != '.')
        return 0;

    path++;

    if (*path == '\0')
        return 1;
    else if (*path != '.')
        return 0;

    path++;
    return (*path == '\0');
}

void lily_fs_Dir_each_entry(lily_state *s)
{
    lily_fs_Dir *d = ARG_Dir(s, 0);
    lily_value *v = lily_arg_value(s, 1);

    if (d->visited == 0)
        d->visited = 1;
    else {
        lily_return_value(s, v);
        return;
    }

    uint16_t file_cid = ID_DirEntry_File(s);
    uint16_t dir_cid = ID_DirEntry_Directory(s);

    lily_call_prepare(s, lily_arg_function(s, 2));

#ifdef _WIN32
    WIN32_FIND_DATA fd;

    while (FindNextFile(d->cursor, &fd)) {
        lily_container_val *variant;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (is_dot_or_dot_dot(fd.cFileName))
                continue;

            variant = lily_push_variant(s, dir_cid, 1);
            lily_push_string(s, fd.cFileName);
        }
        else {
            variant = lily_push_variant(s, file_cid, 1);
            lily_push_string(s, fd.cFileName);
        }

        lily_con_set_from_stack(s, variant, 0);
        lily_push_value(s, v);
        lily_call(s, 2);
    }

    FindClose(d->cursor);
#else
    while (1) {
        struct dirent *e = readdir(d->cursor);

        if (e) {
            lily_container_val *variant;

            if (e->d_type == DT_DIR) {
                if (is_dot_or_dot_dot(e->d_name))
                    continue;

                variant = lily_push_variant(s, dir_cid, 1);
                lily_push_string(s, e->d_name);
            }
            else if (e->d_type == DT_REG) {
                variant = lily_push_variant(s, file_cid, 1);
                lily_push_string(s, e->d_name);
            }
            else
                continue;

            lily_con_set_from_stack(s, variant, 0);
            lily_push_value(s, v);
            lily_call(s, 2);
        }
        else
            break;
    }

    closedir(d->cursor);
#endif

    d->cursor = NULL;
    lily_return_value(s, v);
}

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

static void setup_failure(lily_state *s, const char *message)
{
    lily_container_val *result = lily_push_failure(s);

    lily_push_string(s, message);
    lily_con_set_from_stack(s, result, 0);
}

#ifdef _WIN32
static const char *maybe_load_path(lily_msgbuf *mb, lily_string_val *sv)
{
    uint32_t len = lily_string_length(sv);

    if (len == 0)
        return NULL;

    char *data = lily_string_raw(sv);
    char last_char = data[len];

    lily_mb_add(mb, data);

    if (last_char != '\\')
        lily_mb_add_char(mb, '\\');

    lily_mb_add_char(mb, '*');
    return lily_mb_raw(mb);
}

static void setup_dir_success(lily_state *s, HANDLE data)
#else
static void setup_dir_success(lily_state *s, DIR *data)
#endif
{
    lily_container_val *result = lily_push_success(s);
    lily_fs_Dir *dir = INIT_Dir(s);

    dir->visited = 0;
    dir->cursor = data;
    lily_con_set_from_stack(s, result, 0);
}

void lily_fs__read_dir(lily_state *s)
{
    lily_msgbuf *mb = lily_msgbuf_get(s);

#ifdef _WIN32
    WIN32_FIND_DATA fd;
    lily_string_val *sv = lily_arg_string(s, 0);
    const char *path = lily_string_raw(sv);
    const char *search_path = maybe_load_path(mb, sv);

    if (search_path == NULL) {
        const char *message = lily_mb_sprintf(mb,
                "Not a valid directory path (%s)", path);

        setup_failure(s, message);
        lily_return_top(s);
        return;
    }

    HANDLE h = FindFirstFile(search_path, &fd);

    if (h != INVALID_HANDLE_VALUE) {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            setup_dir_success(s, h);
        else {
            const char *message = lily_mb_sprintf(mb, "Not a directory (%s)",
                    path);
            setup_failure(s, message);
        }
    }
    else {
        const char *message = lily_mb_sprintf(mb,
                "Invalid or forbidden directory (%s)", path);
        setup_failure(s, message);
    }
#else
    const char *path = lily_arg_string_raw(s, 0);
    errno = 0;
    DIR *d = opendir(path);

    if (d)
        setup_dir_success(s, d);
    else {
        char buffer[LILY_STRERROR_BUFFER_SIZE];

        lily_strerror(buffer);

        const char *message = lily_mb_sprintf(mb, "Errno %d: %s (%s).", errno,
                buffer, path);

        setup_failure(s, message);
    }
#endif

    lily_return_top(s);
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
