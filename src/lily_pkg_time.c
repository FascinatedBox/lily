#include <time.h>
#include "lily_alloc.h"
#include "lily_api_embed.h"
#include "lily_api_options.h"
#include "lily_api_value.h"

#include "extras_time.h"

typedef struct {
    LILY_FOREIGN_HEADER

    struct tm local;
} lily_time_Time;

/**
embedded time

The time package provides access to basic time information on the system.
*/

/**
class Time

Instances of this class represent a single point in time. This class also
includes static methods to provide a few extra features.
*/

static void destroy_Time(lily_time_Time *t)
{
}

/**
method Time.clock: Double

Returns the number of seconds of CPU time the interpreter has used.
*/
void lily_time_Time_clock(lily_state *s)
{
    lily_return_double(s, ((double)clock())/(double)CLOCKS_PER_SEC);
}

/**
method Time.now: Time

Returns a `Time` instance representing the current system time.
*/
void lily_time_Time_now(lily_state *s)
{
    lily_time_Time *t = INIT_Time(s);

    time_t raw_time;
    struct tm *time_info;

    time(&raw_time);
    time_info = localtime(&raw_time);
    t->local = *time_info;

    lily_return_foreign(s, (lily_foreign_val *)t);
}

/**
method Time.to_s(self: Time): String

Return a `String` representation of a `Time` instance.

Internally, this calls strftime with `"%Y-%m-%d %H:%M:%S %z"`.

Example output: `"2016-7-10 16:30:00 -0800"`.
*/
void lily_time_Time_to_s(lily_state *s)
{
    lily_time_Time *t = ARG_Time(s, 0);
    char buf[64];

    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %z", &t->local);

    lily_return_string(s, lily_new_string(buf));
}

/**
method Time.since_epoch(self: Time): Integer

Returns the value of `self` as a number of seconds since the epoch.
*/
void lily_time_Time_since_epoch(lily_state *s)
{
    lily_time_Time *t = ARG_Time(s, 0);

    lily_return_integer(s, (int64_t) mktime(&t->local));
}

#include "dyna_time.h"

void lily_pkg_time_init(lily_state *s, lily_options *options)
{
    register_time(s);
}
