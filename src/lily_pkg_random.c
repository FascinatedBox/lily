#include <stdint.h>
#include <time.h>

#include "lily.h"
#define LILY_NO_EXPORT
#include "lily_pkg_random_bindings.h"

/* This code uses the `xoshiro256**` generator, which is in the public domain:
   https://prng.di.unimi.it/xoshiro256starstar.c */

static inline uint64_t rotate_left(const uint64_t x, int k)
{
    return x << k | x >> (64 - k);
}

static uint64_t random_u64(uint64_t state[4])
{
    uint64_t state_0 = state[0];
    uint64_t state_1 = state[1];
    uint64_t state_2 = state[2] ^ state_0;
    uint64_t state_3 = state[3] ^ state_1;

    uint64_t result = rotate_left(state_1 * 5, 7) * 9;

    state[0] = state_0 ^ state_3;
    state[1] = state_1 ^ state_2;
    state[2] = state_2 ^ state_1 << 17;
    state[3] = rotate_left(state_3, 45);

    return result;
}

static inline double random_double(uint64_t state[4])
{
    /* On Windows, `long` is only 32 bits, so we need to use `1LL` instead of
     * `1L` to avoid overflow. */
    return (random_u64(state) >> 11) / (double) (1LL << 53);
}

typedef struct {
    LILY_FOREIGN_HEADER
    uint64_t state[4];
} lily_random_Random;

static void lily_random_destroy_Random(lily_random_Random *r)
{
    (void)r;
}

void lily_random_new_Random(lily_state *s)
{
    lily_random_Random *r = INIT_Random(s);
    int64_t seed = lily_optional_integer(s, 0, 0);
    uint64_t x = (seed > 0 ? (uint64_t) seed : (uint64_t) time(NULL));

    /* The authors of `xoshiro256**` suggest using a `splitmix64` generator to
     * fill the initial state, which is what we do here. This implementation is
     * also in the public domain:
     * https://prng.di.unimi.it/splitmix64.c */
    for (int i = 0;i < 4;i++) {
        uint64_t z = x += 0x9e3779b97f4a7c15;
        z = (z ^ z >> 30) * 0xbf58476d1ce4e5b9;
        z = (z ^ z >> 27) * 0x94d049bb133111eb;
        r->state[i] = z ^ z >> 31;
    }

    lily_return_top(s);
}

void lily_random_Random_between(lily_state *s)
{
    lily_random_Random *r = ARG_Random(s, 0);
    uint64_t rng = random_u64(r->state);

    int64_t start = lily_arg_integer(s, 1);
    int64_t end = lily_arg_integer(s, 2);

    if (start > end)
        lily_ValueError(s, "Interval range is reversed.");

    if (start == end) {
        lily_return_integer(s, start);
        return;
    }

    lily_return_integer(s, start + rng % (end - start + 1));
}

void lily_random_Random_double(lily_state *s)
{
    lily_random_Random *r = ARG_Random(s, 0);
    lily_return_double(s, random_double(r->state));
}

void lily_random_Random_double_between(lily_state *s)
{
    lily_random_Random *r = ARG_Random(s, 0);
    double rng = random_double(r->state);

    double start = lily_arg_double(s, 1);
    double end = lily_arg_double(s, 2);

    if (start > end)
        lily_ValueError(s, "Interval range is reversed.");

    if (start == end) {
	    lily_return_double(s, start);
	    return;
    }

    lily_return_double(s, start + rng * (end - start));
}

LILY_DECLARE_RANDOM_CALL_TABLE
