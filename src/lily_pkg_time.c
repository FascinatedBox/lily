#include <time.h>

#include "lily.h"
#define LILY_NO_EXPORT
#include "lily_pkg_time_bindings.h"

typedef struct {
    LILY_FOREIGN_HEADER
    struct tm local;
} lily_time_Time;

void lily_time_destroy_Time(lily_time_Time *t)
{
    (void)t;
}

void lily_time_Time_clock(lily_state *s)
{
    lily_return_double(s, ((double)clock())/(double)CLOCKS_PER_SEC);
}

void lily_time_Time_now(lily_state *s)
{
    lily_time_Time *t = INIT_Time(s);

    time_t raw_time;
    struct tm *time_info;

    time(&raw_time);
    time_info = localtime(&raw_time);
    t->local = *time_info;

    lily_return_top(s);
}

void lily_time_Time_to_s(lily_state *s)
{
    lily_time_Time *t = ARG_Time(s, 0);
    char buf[64];

    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %z", &t->local);
    lily_return_string(s, buf);
}

void lily_time_Time_since_epoch(lily_state *s)
{
    lily_time_Time *t = ARG_Time(s, 0);

    lily_return_integer(s, (int64_t) mktime(&t->local));
}

LILY_DECLARE_TIME_CALL_TABLE
