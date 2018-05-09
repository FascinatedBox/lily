/**
library time

The time package provides access to basic time information on the system.
*/

#include <time.h>

#include "lily.h"

/** Begin autogen section. **/
typedef struct lily_time_Time_ {
    LILY_FOREIGN_HEADER
    struct tm local;
} lily_time_Time;
#define ARG_Time(state, index) \
(lily_time_Time *)lily_arg_generic(state, index)
#define ID_Time(state) lily_cid_at(state, 0)
#define INIT_Time(state)\
(lily_time_Time *) lily_push_foreign(state, ID_Time(state), (lily_destroy_func)destroy_Time, sizeof(lily_time_Time))

const char *lily_time_info_table[] = {
    "\01Time\0"
    ,"C\04Time\0"
    ,"m\0clock\0: Double"
    ,"m\0now\0: Time"
    ,"m\0to_s\0(Time): String"
    ,"m\0since_epoch\0(Time): Integer"
    ,"Z"
};
void lily_time_Time_clock(lily_state *);
void lily_time_Time_now(lily_state *);
void lily_time_Time_to_s(lily_state *);
void lily_time_Time_since_epoch(lily_state *);
lily_call_entry_func lily_time_call_table[] = {
    NULL,
    NULL,
    lily_time_Time_clock,
    lily_time_Time_now,
    lily_time_Time_to_s,
    lily_time_Time_since_epoch,
};
/** End autogen section. **/

/**
foreign class Time {
    layout {
        struct tm local;
    }
}

Instances of this class represent a single point in time. This class also
includes static methods to provide a few extra features.
*/

void destroy_Time(lily_time_Time *t)
{
}

/**
static define Time.clock: Double

Returns the number of seconds of CPU time the interpreter has used.
*/
void lily_time_Time_clock(lily_state *s)
{
    lily_return_double(s, ((double)clock())/(double)CLOCKS_PER_SEC);
}

/**
static define Time.now: Time

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

    lily_return_top(s);
}

/**
define Time.to_s: String

Return a `String` representation of a `Time` instance.

Internally, this calls strftime with `"%Y-%m-%d %H:%M:%S %z"`.

Example output: `"2016-7-10 16:30:00 -0800"`.
*/
void lily_time_Time_to_s(lily_state *s)
{
    lily_time_Time *t = ARG_Time(s, 0);
    char buf[64];

    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %z", &t->local);

    lily_push_string(s, buf);
    lily_return_top(s);
}

/**
define Time.since_epoch: Integer

Returns the value of `self` as a number of seconds since the epoch.
*/
void lily_time_Time_since_epoch(lily_state *s)
{
    lily_time_Time *t = ARG_Time(s, 0);

    lily_return_integer(s, (int64_t) mktime(&t->local));
}
