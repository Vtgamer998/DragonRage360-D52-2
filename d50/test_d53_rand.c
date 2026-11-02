/* SPDX-License-Identifier: AGPL-3.0-or-later
 * D53-B: the deterministic random source, on its own.
 *
 * Why this test exists: the game draws with rand() 363 times across 46 files
 * and some of those draws are in the simulation. For two consoles to agree,
 * their generators must produce the same numbers, so src/dr_rand.c has to be
 * newlib's rand() bit for bit -- same algorithm, same initial state, same
 * RAND_MAX. Everything else about it (the exposed state, the draw counter) is
 * what lets the battle hash notice a divergence in the number of draws.
 *
 * The expected values are computed here from newlib's own constants
 * (newlib/libc/stdlib/rand.c: state = state * 6364136223846793005LL + 1,
 * return (state >> 32) & RAND_MAX, state starts at 1), not read back from the
 * implementation under test. */
#include "dr_rand.h"
#include <stdio.h>
#include <stdint.h>

#define NEWLIB_MULTIPLIER 6364136223846793005ULL
#define NEWLIB_INCREMENT  1ULL
#define NEWLIB_INITIAL    1ULL
#define NEWLIB_RAND_MAX   0x7fffffffu

static int fail(int code,const char *msg)
{
    printf("FAIL: %s (code=%d)\n",msg,code);
    return code;
}

/* The state newlib would hold after `n` draws from a fresh generator. */
static uint64_t newlib_state(int n)
{
    uint64_t st=NEWLIB_INITIAL;
    int i;
    for(i=0;i<n;i++) st=st*NEWLIB_MULTIPLIER+NEWLIB_INCREMENT;
    return st;
}

int main(void)
{
    uint64_t st;
    uint32_t first,again,value;
    int i;

    /* ---- a fresh generator is newlib's -------------------------------- */
    dr_rand_reset();
    if(dr_rand_state()!=NEWLIB_INITIAL)
        return fail(-1,"a fresh generator must start at newlib's state (1)");
    if(dr_rand_draws()!=0u)
        return fail(-2,"a fresh generator must have served no draws");

    /* ---- the first draws are newlib's --------------------------------- */
    for(i=1;i<=5;i++) {
        value=dr_rand();
        st=newlib_state(i);
        if(value!=(uint32_t)((st>>32)&(uint64_t)NEWLIB_RAND_MAX))
            return fail(-3,"a draw must equal newlib's rand() for the same history");
        if(dr_rand_state()!=st)
            return fail(-4,"the state must equal newlib's after the same draws");
        if(value>NEWLIB_RAND_MAX)
            return fail(-5,"a draw must stay inside RAND_MAX");
    }
    if(dr_rand_draws()!=5u)
        return fail(-6,"the draw counter must count every draw");

    /* ---- srand seeds exactly as newlib does --------------------------- *
     * newlib's srand only stores the seed, and so does dr_srand: after it the
     * next draw must be the first draw of a generator seeded with that value. */
    dr_srand(12345u);
    if(dr_rand_state()!=12345u)
        return fail(-7,"srand must store the seed as the state");
    value=dr_rand();
    st=12345u;
    st=st*NEWLIB_MULTIPLIER+NEWLIB_INCREMENT;
    if(value!=(uint32_t)((st>>32)&(uint64_t)NEWLIB_RAND_MAX))
        return fail(-8,"the first draw after srand must follow the formula");
    if(dr_rand_draws()!=6u)
        return fail(-9,"srand must not clear the draw counter");

    /* ---- reset puts a rematch back at the beginning ------------------- */
    dr_rand_reset();
    if(dr_rand_state()!=NEWLIB_INITIAL || dr_rand_draws()!=0u)
        return fail(-10,"reset must return to newlib's fresh state");
    first=dr_rand();
    dr_rand_reset();
    again=dr_rand();
    if(first!=again)
        return fail(-11,"two resets must reproduce the same first draw");

    /* ---- the sequence does not repeat quickly ------------------------- *
     * A generator that wrapped back to its start would make two different
     * histories look identical to the hash. */
    dr_rand_reset();
    for(i=0;i<1000;i++) {
        value=dr_rand();
        if(value>NEWLIB_RAND_MAX)
            return fail(-12,"every draw must stay inside RAND_MAX");
    }
    if(dr_rand_draws()!=1000u)
        return fail(-13,"a thousand draws must be counted as a thousand");
    if(dr_rand_state()==NEWLIB_INITIAL)
        return fail(-14,"the state must have moved away from the start");

    printf("PASS: dr_rand matches newlib's rand() (state 1, LCG "
           "6364136223846793005, RAND_MAX 0x7FFFFFFF), reseeds and resets "
           "cleanly\n");
    return 0;
}
