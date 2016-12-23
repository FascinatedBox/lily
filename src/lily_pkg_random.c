/* 
   A C-program for MT19937-64 (2004/9/29 version).
   Coded by Takuji Nishimura and Makoto Matsumoto.

   This is a 64-bit version of Mersenne Twister pseudorandom number
   generator.

   Copyright (C) 2004, Makoto Matsumoto and Takuji Nishimura,
   All rights reserved.                          

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

     1. Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.

     2. Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.

     3. The names of its contributors may not be used to endorse or promote 
        products derived from this software without specific prior written 
        permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   References:
   T. Nishimura, ``Tables of 64-bit Mersenne Twisters''
     ACM Transactions on Modeling and 
     Computer Simulation 10. (2000) 348--357.
   M. Matsumoto and T. Nishimura,
     ``Mersenne Twister: a 623-dimensionally equidistributed
       uniform pseudorandom number generator''
     ACM Transactions on Modeling and 
     Computer Simulation 8. (Jan. 1998) 3--30.

   Any feedback is very welcome.
   http://www.math.hiroshima-u.ac.jp/~m-mat/MT/emt.html
   email: m-mat @ math.sci.hiroshima-u.ac.jp (remove spaces)
*/

#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include "lily_api_alloc.h"
#include "lily_api_embed.h"
#include "lily_api_value.h"
#include "extras_random.h"

#define NN 312
#define MM 156
#define MATRIX_A 0xB5026F5AA96619E9ULL
#define UM 0xFFFFFFFF80000000ULL /* Most significant 33 bits */
#define LM 0x7FFFFFFFULL /* Least significant 31 bits */

typedef struct {
    LILY_FOREIGN_HEADER

    /* The array for the state vector */
    unsigned long long mt[NN]; 

    /* mti==NN+1 means mt[NN] is not initialized */
    int mti;
} lily_random_Random;

static void destroy_Random(lily_random_Random *r)
{
    lily_free(r);
}

/**
embedded random

The random package provides access to a Mersenne Twister for pseudo-random
number generation.
*/

/**
class Random

The `Random` class provides access to the random number generator. Each
instance is completely separate from all others.

The constructor for this class takes a seed. If the seed provided is 0 or less,
then the current time (`time(NULL)` in C) is used instead. 
*/

/**
constructor Random(seed: *Integer=0): Random
*/
void lily_random_Random_new(lily_state *s)
{
    lily_random_Random *r;
    int64_t seed = 0;
    int mti;

    INIT_Random(s, r);

    if (lily_arg_count(s) == 2)
        seed = lily_arg_integer(s, 1);

    if (seed <= 0)
        seed = (int64_t)time(NULL);

    r->mt[0] = seed;
    for (mti=1; mti<NN; mti++) 
        r->mt[mti] = (6364136223846793005ULL * (r->mt[mti-1] ^ (r->mt[mti-1] >> 62)) + mti);

    r->mti = mti;
    lily_return_foreign(s, ID_Random(s), (lily_foreign_val *)r);
}

/* generates a random number on [0, 2^64-1]-interval */
static uint64_t gen_int(lily_random_Random *r)
{
    int i;
    uint64_t x;
    static unsigned long long mag01[2] = {0ULL, MATRIX_A};

    if (r->mti >= NN) { /* generate NN words at one time */
        for (i=0;i<NN-MM;i++) {
            x = (r->mt[i]&UM)|(r->mt[i+1]&LM);
            r->mt[i] = r->mt[i+MM] ^ (x>>1) ^ mag01[(int)(x&1ULL)];
        }
        for (;i<NN-1;i++) {
            x = (r->mt[i]&UM)|(r->mt[i+1]&LM);
            r->mt[i] = r->mt[i+(MM-NN)] ^ (x>>1) ^ mag01[(int)(x&1ULL)];
        }
        x = (r->mt[NN-1]&UM)|(r->mt[0]&LM);
        r->mt[NN-1] = r->mt[MM-1] ^ (x>>1) ^ mag01[(int)(x&1ULL)];

        r->mti = 0;
    }
  
    x = r->mt[r->mti++];

    x ^= (x >> 29) & 0x5555555555555555ULL;
    x ^= (x << 17) & 0x71D67FFFEDA60000ULL;
    x ^= (x << 37) & 0xFFF7EEE000000000ULL;
    x ^= (x >> 43);

    return x;
}

/**
method Random.between(self: Random, lower: Integer, upper: Integer): Integer

Generate a random `Integer` value between `lower` and `upper`.

# Errors

* `ValueError` is raised if the range is empty, or reversed.
*/
void lily_random_Random_between(lily_state *s)
{
    lily_random_Random *r = ARG_Random(s, 0);
    uint64_t rng = gen_int(r);

    int64_t start = lily_arg_integer(s, 1);
    int64_t end = lily_arg_integer(s, 2);

    if (start >= end)
        lily_ValueError(s, "Interval range is empty.");

    int64_t distance = end - start + 1;
    uint64_t result = start + (rng % distance);

    lily_return_integer(s, result);
}

#include "dyna_random.h"

void lily_pkg_random_init(lily_state *s, lily_options *options)
{
    register_random(s);
}
