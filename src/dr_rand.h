/* SPDX-License-Identifier: MIT
 * D53-B: the deterministic random source.
 *
 * WHY THIS EXISTS
 *
 * The original game draws with rand() 363 times across 46 files, and some of
 * those draws sit in the simulation (btl_char_hit.c damage, btl_seq.c, gsc.c),
 * not only in drawing code. srand() is never called anywhere, so both consoles
 * start from the libc default seed and produce the same sequence -- PROVIDED
 * they call rand() the same number of times. A common seed is necessary but
 * not sufficient: if the two execution paths ever diverge, the sequences drift
 * apart and nothing in gBattleWork would show it, because the generator's
 * state lives inside the C library, not in the battle work block.
 *
 * WHAT THIS MODULE DOES
 *
 * dr_rand() reproduces newlib's rand() bit for bit (newlib 4.6, the SDK's C
 * library: a 64-bit LCG seeded from 1, constants from Knuth), so the game
 * keeps the exact sequence it produces today. The difference is that the state
 * is kept HERE, where the battle hash can read it with dr_rand_state().
 *
 * Because that state is a pure function of the number of draws, hashing it
 * turns "the two consoles called rand() a different number of times" into a
 * hash mismatch on the very frame it happens -- instead of a silent drift.
 *
 * Building with -DDR_PROVIDE_LIBC_RAND also defines rand() and srand()
 * themselves, so every draw the engine makes goes through this counter. That
 * is the XEX build; host tests leave the platform's rand() alone and drive
 * dr_rand() directly.
 */
#ifndef DR_RAND_H
#define DR_RAND_H
#include <stdint.h>

/* One draw. Same value newlib's rand() would return for the same history. */
uint32_t dr_rand(void);

/* Reseed, exactly as srand() does: the state becomes the seed itself. */
void dr_srand(unsigned int seed);

/* The generator's state after the last draw. Both consoles must carry the
 * same value after the same frame; a difference means the two sides drew a
 * different number of times, which means their execution paths diverged. */
uint64_t dr_rand_state(void);

/* How many draws happened since the last reset. Diagnostics only. */
uint32_t dr_rand_draws(void);

/* Back to the state a fresh process starts in (newlib initialises to 1). */
void dr_rand_reset(void);

#endif
