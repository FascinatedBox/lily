#include "lily.h"
#include "lily_value.h"
#define LILY_NO_EXPORT
#include "lily_pkg_subprocess_bindings.h"

#ifdef _WIN32
# define lily_popen(command_, mode_) (_popen(command_, mode_))
# define lily_pclose(f_)             _pclose(f_)
#else
# define lily_popen(command_, mode_) (fflush(NULL), popen(command_, mode_))
# define lily_pclose(f_)             pclose(f_)
#endif

static void file_pclose(FILE *f)
{
    lily_pclose(f);
}

void lily_subprocess__popen(lily_state *s)
{
    char *command = lily_arg_string_raw(s, 0);
    const char *mode = lily_optional_string_raw(s, 1, "r");
    const char *mode_ch = mode;
    int ok;

    if (*mode_ch == 'r' || *mode_ch == 'w') {
        mode_ch++;
        ok = (*mode_ch == '\0');
    }
    else
        ok = 0;

    if (ok == 0)
        lily_ValueError(s, "Invalid mode '%s' given.", mode);

    FILE *f = lily_popen(command, mode);

    if (f == NULL)
        lily_RuntimeError(s, "Failed to run command.");

    lily_push_file(s, f, mode, file_pclose);
    lily_return_top(s);
}

LILY_DECLARE_SUBPROCESS_CALL_TABLE
