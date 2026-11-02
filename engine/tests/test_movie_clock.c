/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_movie_clock.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    XbMovieClock c={0};assert(XbMovieClock_Due(&c,0,100,0,0)==1);
    assert(!XbMovieClock_Due(&c,1,10000000,0,0));
    assert(!XbMovieClock_Due(&c,1,20000000,33366665,0));
    assert(XbMovieClock_Due(&c,1,33366666,33366666,0)==1);
    /* Queue-ahead/wall clock alone cannot release the next picture. */
    assert(!XbMovieClock_Due(&c,2,500000000,33366666,0));
    assert(XbMovieClock_Due(&c,2,600000000,70000000,0)==1);
    assert(XbMovieClock_Due(&c,3,610000000,60000000,0)==-1);
    assert(!XbMovieClock_Due(&c,100,620000000,70000000,1));
    assert(XbMovieClock_Due(&c,100,4000000000ull,70000000,1)==1);
    c=(XbMovieClock){0};assert(!XbMovieClock_Due(&c,1,0,0,0));
    assert(XbMovieClock_Due(&c,1,2000000001ull,0,0)==-1);
    puts("PASS: 30000/1001 pacing, SDK-media master, late frames, underrun hold, regression/stall refusal, explicit audio-end tail");return 0;
}
