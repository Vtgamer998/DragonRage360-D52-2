/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Host test for D52-1. It runs the real UDP lobby state machine over
 * 127.0.0.1. It does not prove the Xbox 360 network stack, a router, Wi-Fi or
 * a two-console session. */
#include "lobby_selftest.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    int result=D52_LobbySelfTest();
    assert(result==0);
    puts("PASS: D52-1 loopback lobby selftest, host only");
    return 0;
}
