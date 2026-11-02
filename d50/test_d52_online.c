/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Host test for D52-2. It runs the real UDP lobby state machine
 * over 127.0.0.1 and the online battle synchronization. */
#include "online_battle_selftest.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    int result=D52_OnlineBattleSelfTest();
    assert(result==0);
    puts("PASS: D52-2 loopback online battle selftest, host only");
    return 0;
}