/* SPDX-License-Identifier: MIT
 * D53-B: the deterministic random source. See dr_rand.h for why.
 *
 * The algorithm and the initial state are newlib's, copied from
 * newlib/libc/stdlib/rand.c (newlib 4.6.0, the SDK's C library):
 *
 *     _REENT_RAND_NEXT(reent) = _REENT_RAND_NEXT(reent) * 6364136223846793005LL + 1;
 *     return (int)((_REENT_RAND_NEXT(reent) >> 32) & RAND_MAX);
 *
 * with _REENT_RAND_NEXT initialised to 1 (sys/reent.h, _REENT_INIT_PTR). The
 * multiplier is Knuth's (The Art of Computer Programming, vol 2, p.106).
 * Keeping those constants means the game draws exactly the numbers it draws
 * today; what changes is only where the state lives.
 */
#include "dr_rand.h"
#include <stdlib.h>

#define DR_LCG_MULTIPLIER 6364136223846793005ULL
#define DR_LCG_INCREMENT  1ULL

/* newlib starts _rand_next at 1, so a fresh process and a reset agree. */
static uint64_t s_state = 1;
static uint32_t s_draws = 0;

uint32_t dr_rand(void)
{
    s_state = s_state * DR_LCG_MULTIPLIER + DR_LCG_INCREMENT;
    s_draws++;
    /* RAND_MAX is 0x7fffffff with a 32-bit int on both newlib and glibc, so
     * this is the same value the C library would return. */
    return (uint32_t)((s_state >> 32) & (uint64_t)RAND_MAX);
}

void dr_srand(unsigned int seed)
{
    s_state = (uint64_t)seed;
}

uint64_t dr_rand_state(void)
{
    return s_state;
}

uint32_t dr_rand_draws(void)
{
    return s_draws;
}

void dr_rand_reset(void)
{
    s_state = 1;
    s_draws = 0;
}

#ifdef DR_PROVIDE_LIBC_RAND
/* Every draw the engine makes lands in the counter above.
 *
 * This replaces the C library's rand()/srand() in the XEX build. The
 * sequence is unchanged (same algorithm, same initial state), but the state
 * is now readable by the battle hash, so a divergence in the number of draws
 * shows up as a hash mismatch instead of a silent drift.
 *
 * Host tests do not define DR_PROVIDE_LIBC_RAND: they keep the platform's
 * rand() and drive dr_rand() directly, so no existing test changes behaviour.
 */
int rand(void)
{
    return (int)dr_rand();
}

void srand(unsigned int seed)
{
    dr_srand(seed);
}
#endif
