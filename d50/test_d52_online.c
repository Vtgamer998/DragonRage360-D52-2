/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Host test for D52-2. It runs the real UDP lobby state machine and the real
 * lockstep harness over 127.0.0.1. It does not prove the Xbox 360 network
 * stack, a router, Wi-Fi or a two-console session. */
#include "online_battle_selftest.h"
#include <stdio.h>

int main(void)
{
    int result=D52_OnlineBattleSelfTest();
    if(result) {
        printf("FAIL: D52-2 loopback online battle selftest, code=%d\n",result);
        return 1;
    }
    puts("PASS: D52-2 loopback online battle selftest, host only");
    return 0;
}
