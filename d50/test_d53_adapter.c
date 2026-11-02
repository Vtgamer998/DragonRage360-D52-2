/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Host test for D53-A. It runs the engine adapter contract against the
 * reference backend, and (when the build defines DR_HAVE_BT3XBX) the bridge
 * to the battle engine against the stub engine.
 *
 * What this does NOT prove: that the real bt3xbx fight links, runs on a
 * console, or is deterministic end to end. The reference backend proves the
 * adapter contract; the stub proves the call sequence and the pad injection;
 * the real engine needs D53-B, on the Xbox 360. */
#include "engine_adapter_selftest.h"
#include <stdio.h>

#ifdef DR_HAVE_BT3XBX
#include "bt3xbx_adapter_selftest.h"
#endif

int main(void)
{
    int result;

    result=D53_EngineAdapterSelfTest();
    if(result) {
        printf("FAIL: D53-A engine adapter selftest, code=%d\n",result);
        return 1;
    }
    puts("PASS: D53-A engine adapter selftest (lifecycle, determinism, lockstep)");

#ifdef DR_HAVE_BT3XBX
    result=D53_Bt3xbxAdapterSelfTest();
    if(result) {
        printf("FAIL: D53-A bt3xbx bridge selftest, code=%d\n",result);
        return 1;
    }
    puts("PASS: D53-A bt3xbx bridge selftest (call sequence, pad injection, hash)");
#else
    puts("SKIP: D53-A bt3xbx bridge selftest (engine not linked; build d53-bridge-test)");
#endif

    return 0;
}
