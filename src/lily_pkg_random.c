#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "lily.h"
#define LILY_NO_EXPORT
#include "lily_pkg_random_bindings.h"

/* This uses code from libmtwist which uses The Unlicense:
   https://github.com/dajobe/libmtwist
   https://unlicense.org/

   This randomness library is limited to 32 bit values. It's assumed that most
   ranges will be within 32 bits (4 billion-ish). */

#define MTWIST_N             624
#define MTWIST_M             397
#define MTWIST_UPPER_MASK    ((uint32_t)0x80000000)
#define MTWIST_LOWER_MASK    ((uint32_t)0x7FFFFFFF)
#define MTWIST_FULL_MASK     ((uint32_t)0xFFFFFFFF)
#define MTWIST_MATRIX_A      ((uint32_t)0x9908B0DF)

#define MTWIST_MIXBITS(u, v) ( ( (u) & MTWIST_UPPER_MASK) | ( (v) & MTWIST_LOWER_MASK) )
#define MTWIST_TWIST(u, v)  ( (MTWIST_MIXBITS(u, v) >> 1) ^ ( (v) & UINT32_C(1) ? MTWIST_MATRIX_A : UINT32_C(0)) )

typedef struct {
    LILY_FOREIGN_HEADER
    uint32_t state[MTWIST_N];
    uint32_t *next;
    int remaining;
} lily_random_Random;

static void lily_random_destroy_Random(lily_random_Random *r)
{
    (void)r;
}

void lily_random_new_Random(lily_state *s)
{
    lily_random_Random* mt = INIT_Random(s);
    int64_t seed = 0;
    int i;

    if (lily_arg_count(s) == 2)
        seed = lily_arg_integer(s, 1);

    if (seed <= 0)
        seed = (int64_t)time(NULL);

    mt->state[0] = (uint32_t)(seed & MTWIST_FULL_MASK);
    for(i = 1; i < MTWIST_N; i++) {
        mt->state[i] = (((uint32_t)1812433253) * (mt->state[i - 1] ^ (mt->state[i - 1] >> 30)) + i);
        mt->state[i] &= MTWIST_FULL_MASK;
    }

    mt->remaining = 0;
    mt->next = NULL;

    lily_return_top(s);
}

static void mtwist_update(lily_random_Random* mt)
{
    int count;
    uint32_t *p = mt->state;

    for (count = (MTWIST_N - MTWIST_M + 1); --count; p++)
        *p = p[MTWIST_M] ^ MTWIST_TWIST(p[0], p[1]);

    for (count = MTWIST_M; --count; p++)
        *p = p[MTWIST_M - MTWIST_N] ^ MTWIST_TWIST(p[0], p[1]);

    *p = p[MTWIST_M - MTWIST_N] ^ MTWIST_TWIST(p[0], mt->state[0]);

    mt->remaining = MTWIST_N;
    mt->next = mt->state;
}

uint32_t mtwist_u32rand(lily_random_Random* mt)
{
    uint32_t r;

    if (mt->remaining == 0)
        mtwist_update(mt);

    r = *mt->next++;
    mt->remaining--;

    /* Tempering. */
    r ^= (r >> 11);
    r ^= (r << 7) & ((uint32_t)0x9D2C5680);
    r ^= (r << 15) & ((uint32_t)0xEFC60000);
    r ^= (r >> 18);

    r &= MTWIST_FULL_MASK;

    return (uint32_t)r;
}

/* This is not libmtwist's original implementation of this function. Instead, we
 * generate two random u32 values, and combine bits from each to create a
 * double. This gives us more possible values than with libmtwist's
 * implementation, which used only a single random u32.
 *
 * This approach is described here:
 * https://www.sciencedirect.com/science/article/abs/pii/S0378475418302040 */
double mtwist_drand(lily_random_Random* mt)
{
    uint32_t high = mtwist_u32rand(mt) >> 5;  /* Get 27 bits. */
    uint32_t low = mtwist_u32rand(mt) >> 6;   /* Get 26 bits. */

    /* Combine them to get the 53 bits we need for a double. */
    return (high * 67108864.0 + low) * (1.0 / 9007199254740992.0);
}

void lily_random_Random_between(lily_state *s)
{
    lily_random_Random *r = ARG_Random(s, 0);
    uint64_t rng = (uint64_t)mtwist_u32rand(r);

    int64_t start = lily_arg_integer(s, 1);
    int64_t end = lily_arg_integer(s, 2);

    if (start > end)
        lily_ValueError(s, "Interval range is reversed.");

    if (start == end) {
        lily_return_integer(s, start);
	return;
    }

    int64_t distance = end - start + 1;

    if (distance < INT32_MIN ||
        distance > INT32_MAX)
        lily_ValueError(s, "Interval exceeds 32 bits in size.");

    uint64_t result = start + (rng % distance);

    lily_return_integer(s, result);
}

void lily_random_Random_double(lily_state *s)
{
    lily_random_Random *r = ARG_Random(s, 0);
    lily_return_double(s, mtwist_drand(r));
}

void lily_random_Random_double_between(lily_state *s)
{
    lily_random_Random *r = ARG_Random(s, 0);
    double rng = mtwist_drand(r);

    double start = lily_arg_double(s, 1);
    double end = lily_arg_double(s, 2);

    if (start > end)
        lily_ValueError(s, "Interval range is reversed.");

    if (start == end) {
	lily_return_double(s, start);
	return;
    }

    double result = start + rng * (end - start);

    lily_return_double(s, result);
}

LILY_DECLARE_RANDOM_CALL_TABLE
