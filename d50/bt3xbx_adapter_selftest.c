/* SPDX-License-Identifier: AGPL-3.0-or-later
 * D53-A: host test of src/bt3xbx_adapter.c, the bridge to the real engine.
 *
 * The engine itself is replaced by d50/bt3xbx_stub.c, which implements the
 * four symbols the adapter calls and reads the pads through gXbPadPoll exactly
 * as the engine does. That lets this test prove, on a PC:
 *
 *   - the call sequence is right: Battle_Init once per start, one
 *     Battle_StepFrame per update, Battle_Term once per end, and no double
 *     start / double end (the phase machine's guarantee);
 *   - the pad injection works end to end: the stub reads gXbPadPoll and sees
 *     the SYNCHRONIZED words, not the local controller;
 *   - gXbPadPoll is restored when the match ends, so offline play keeps
 *     reading the real pad;
 *   - the hash moves with the inputs, and is stable across a repeated run of
 *     the same input sequence (the singleton engine's form of determinism);
 *   - the hash SKIPS BattleMember.data: poking a loader heap pointer does not
 *     change it, which is the rule that keeps heap addresses out of the
 *     comparison between two consoles.
 *
 * It does NOT prove the real engine links, runs, or is deterministic: the stub
 * is not the fight. That is D53-B, on the console.
 */
#include "bt3xbx_stub.h"
#include "engine_adapter.h"
#include "bt3xbx_adapter.h"
#include "dr_rand.h"
#include "xsdk_compat.h"
#include "xb_pad.h"
#include <string.h>

#define CHECK(cond,code,msg) do { \
    if(!(cond)) { D50_Log("FAIL: %s",(msg)); result=(code); goto out; } \
} while(0)

/* XINPUT_GAMEPAD_* bits, as xb_plat_pad.c uses them. */
enum { X_UP=0x0001, X_DOWN=0x0002, X_LEFT=0x0004, X_RIGHT=0x0008,
       X_A=0x1000, X_B=0x2000 };

static uint32_t pattern_a(uint32_t frame)
{
    uint32_t in=0;
    if((frame/3u)%2u) in|=1u<<0; else in|=1u<<1;   /* left / right */
    if((frame/5u)%2u) in|=1u<<4;                   /* A */
    return in;
}
static uint32_t pattern_b(uint32_t frame)
{
    uint32_t in=0;
    if((frame/2u)%2u) in|=1u<<2; else in|=1u<<3;   /* up / down */
    if((frame/7u)%2u) in|=1u<<1;                   /* right */
    return in;
}

/* Runs `frames` frames of one input stream through the adapter and returns
 * the hash of the last one. Leaves the adapter RUNNING. */
static int run_frames(DrEngineAdapter *a,int frames,uint32_t *last_hash)
{
    int i;
    for(i=1;i<=frames;i++) {
        int rc=dr_engine_update(a,pattern_a((uint32_t)i),pattern_b((uint32_t)i));
        if(rc<0) return -1;
    }
    *last_hash=dr_engine_hash(a);
    return 0;
}

int D53_Bt3xbxAdapterSelfTest(void)
{
    DrEngineAdapter a;
    DrBt3xbxState state;
    int (*saved_poll)(int,XbPadState *);
    uint32_t hash_a=0,hash_b=0,hash_c=0;
    uint16_t buttons[2];
    int result=0;
    int i;

    memset(&a,0,sizeof a);
    memset(&state,0,sizeof state);
    XbStub_Reset();
    saved_poll=gXbPadPoll;              /* what the console had before us */

    D50_Log("D53-A bt3xbx bridge test: stub engine, not the real fight");

    /* ---- bind and start ---------------------------------------------- */
    CHECK(dr_engine_bind(&a,&dr_bt3xbx_ops,&state)==0,-1,
          "bind of the bt3xbx backend must succeed");
    CHECK(strcmp(dr_engine_backend_name(&a),"bt3xbx")==0,-2,
          "the backend must name itself bt3xbx");
    /* D53-B: leave the generator in a state no battle may inherit. The menus
     * draw with rand() and the two consoles navigate them independently, so
     * start must reseed BEFORE Battle_Init. */
    dr_srand(0xDEADBEEFu);
    (void)dr_rand();(void)dr_rand();(void)dr_rand();
    CHECK(dr_rand_draws()==3u,-90,"the poison must be counted");
    CHECK(dr_engine_start(&a)==0,-3,"start must succeed");
    CHECK(XbStub_InitCalls()==1,-4,"start must call Battle_Init exactly once");
    CHECK(dr_engine_phase(&a)==DR_ENGINE_RUNNING,-5,"start must be RUNNING");
    CHECK(gXbPadPoll!=saved_poll,-6,
          "start must install the pad injection seam");
    CHECK(dr_engine_start(&a)!=0,-7,"a second start must fail");
    CHECK(XbStub_InitCalls()==1,-8,
          "a refused second start must not call Battle_Init again");
    /* The stub draws twice inside Battle_Init, so the generator must now be
     * exactly "newlib's initial state (1) advanced twice": the reseed wiped
     * the poison (draw count back to 0) and the two draws landed on it. The
     * expected state is computed here from newlib's own constants instead of
     * being taken from the implementation under test. */
    {
        uint64_t st=1;
        st=st*6364136223846793005ULL+1ULL;
        st=st*6364136223846793005ULL+1ULL;
        CHECK(dr_rand_draws()==2u,-91,
              "start must reseed the generator before Battle_Init");
        CHECK(dr_rand_state()==st,-92,
              "the battle must begin from newlib's fresh state, twice drawn");
    }
    D50_Log("PASS: D53-A bt3xbx start calls Battle_Init once and installs the seam");
    D50_Log("PASS: D53-B bt3xbx start reseeds the RNG before Battle_Init "
            "(draws=%u state=%08X%08X)",
            (unsigned)dr_rand_draws(),
            (unsigned)(dr_rand_state()>>32),(unsigned)dr_rand_state());

    /* ---- the injected pads reach the engine --------------------------- *
     * Frame 1: pad 0 holds "left", pad 1 holds "A". The stub reads them back
     * through gXbPadPoll, which is the same call the engine's Pad_Update
     * makes. If the seam were missing, both would read the absent local
     * controller and come back 0. */
    CHECK(dr_engine_update(&a,1u<<0,1u<<4)==0,-9,"the first frame must run");
    XbStub_LastButtons(buttons);
    CHECK(buttons[0]==(uint16_t)X_LEFT,-10,
          "pad 0 must receive the synchronized 'left' bit");
    CHECK(buttons[1]==(uint16_t)X_A,-11,
          "pad 1 must receive the synchronized 'A' bit");
    CHECK(XbStub_PollCalls()>=2,-12,"the stub must have polled both pads");
    D50_Log("PASS: D53-A bt3xbx synchronized inputs reach the engine "
            "(pad0=%04X pad1=%04X)",
            (unsigned)buttons[0],(unsigned)buttons[1]);

    /* ---- the hash moves with the frames ------------------------------- */
    for(i=2;i<=20;i++)
        CHECK(dr_engine_update(&a,pattern_a((uint32_t)i),
                               pattern_b((uint32_t)i))==0,-13,
              "every frame must simulate");
    hash_a=dr_engine_hash(&a);
    CHECK(hash_a!=0,-14,"the hash must be non-zero while running");
    CHECK(dr_engine_update(&a,0,0)==0,-15,"another frame");
    CHECK(dr_engine_hash(&a)!=hash_a,-16,
          "the hash must change as the simulation advances");
    D50_Log("PASS: D53-A bt3xbx hash moves with the simulation (%08X)",
            (unsigned)hash_a);

    /* ---- the hash skips the loader's heap pointers -------------------- *
     * BattleMember.data is a heap address. If it were hashed, two consoles
     * could disagree for a reason that means nothing. Poking it must not move
     * the hash. */
    {
        uint32_t before=dr_engine_hash(&a);
        XbStub_SetLoaderPointer((void *)(uintptr_t)0x11112222u);
        CHECK(dr_engine_hash(&a)==before,-17,
              "poking BattleMember.data must not change the hash");
        XbStub_SetLoaderPointer((void *)(uintptr_t)0x33334444u);
        CHECK(dr_engine_hash(&a)==before,-18,
              "a second different pointer must not change the hash either");
        XbStub_SetLoaderPointer(0);
        CHECK(dr_engine_hash(&a)==before,-19,
              "clearing the pointer must not change the hash");
    }
    D50_Log("PASS: D53-A bt3xbx hash skips BattleMember.data (heap pointers "
            "stays out of the comparison)");

    /* ---- D53-B: the hash covers the WHOLE simulation ------------------ *
     * Two equal hashes over gBattleWork prove nothing about what lives
     * outside it. Each poke below targets one block the D53-B hash added, and
     * each must move the hash; the last one targets the change queue's `cur`
     * member, an address, and must NOT move it. */
    {
        uint32_t h;
        h=dr_engine_hash(&a);
        XbStub_SetFighterAction(0,0x5150);
        CHECK(dr_engine_hash(&a)!=h,-100,
              "a fighter's own bytes must be part of the hash");
        h=dr_engine_hash(&a);
        XbStub_SetFighterAction(1,0x5151);
        CHECK(dr_engine_hash(&a)!=h,-101,
              "the second fighter must be part of the hash too");
        h=dr_engine_hash(&a);
        XbStub_SetRosterFrame(0x2222);
        CHECK(dr_engine_hash(&a)!=h,-102,
              "the roster's frame counter must be part of the hash");
        h=dr_engine_hash(&a);
        XbStub_SetFighterRand(0x3333u);
        CHECK(dr_engine_hash(&a)!=h,-103,
              "the fighters' own random state must be part of the hash");
        h=dr_engine_hash(&a);
        XbStub_SetAiFrame(0x4444);
        CHECK(dr_engine_hash(&a)!=h,-104,
              "the AI's frame counter must be part of the hash");
        h=dr_engine_hash(&a);
        XbStub_SetScript(3,1);
        CHECK(dr_engine_hash(&a)!=h,-105,
              "the battle script's state must be part of the hash");
        h=dr_engine_hash(&a);
        XbStub_SetChange(4,1);         /* BTL_CHANGE_LOADED, time stopped */
        CHECK(dr_engine_hash(&a)!=h,-106,
              "the character change queue must be part of the hash");
        D50_Log("PASS: D53-B bt3xbx hash covers fighters, roster, second RNG, "
                "AI, script and change queue");

        /* The libc generator's state: it moves whenever a draw happens, so a
         * single extra draw must change the hash -- that is what turns "the
         * two consoles called rand() a different number of times" into a
         * mismatch on the frame it happens. */
        h=dr_engine_hash(&a);
        (void)dr_rand();
        CHECK(dr_engine_hash(&a)!=h,-107,
              "the libc RNG state must be part of the hash");
        h=dr_engine_hash(&a);
        dr_rand_reset();
        CHECK(dr_engine_hash(&a)!=h,-108,
              "reseeded RNG state must hash differently again");
        D50_Log("PASS: D53-B bt3xbx hash covers the libc RNG state");

        /* The change queue's `cur` points into the queue itself. It is an
         * address, and on the console the image sits at a random one, so it
         * must stay out of the comparison. */
        h=dr_engine_hash(&a);
        XbStub_SetChangeCur((void *)(uintptr_t)0xC0FFEEu);
        CHECK(dr_engine_hash(&a)==h,-109,
              "the change queue's pointer must not be hashed");
        XbStub_SetChangeCur(0);
        CHECK(dr_engine_hash(&a)==h,-110,
              "clearing that pointer must not move the hash either");
        D50_Log("PASS: D53-B bt3xbx hash keeps the change queue's pointer out "
                "of the comparison");
    }

    /* ---- battle over, then a single clean end ------------------------- */
    while(!a.finished) {
        int rc=dr_engine_update(&a,0,0);
        CHECK(rc>=0,-20,"the stub battle must run to its end");
        if(rc==1) break;
        CHECK(XbStub_Frames()<=BT3XBX_STUB_FRAMES+1,-21,
              "the stub battle must terminate");
    }
    CHECK(a.finished,-22,"update returning 1 must mark the battle finished");
    CHECK(dr_engine_phase(&a)==DR_ENGINE_RUNNING,-23,
          "a finished battle must stay endable");
    CHECK(dr_engine_end(&a)==0,-24,"end must succeed");
    CHECK(XbStub_TermCalls()==1,-25,"end must call Battle_Term exactly once");
    CHECK(gXbPadPoll==saved_poll,-26,
          "end must restore the real controller for offline play");
    CHECK(dr_engine_end(&a)!=0,-27,"a second end must fail (no double Term)");
    CHECK(XbStub_TermCalls()==1,-28,
          "a refused second end must not call Battle_Term again");
    CHECK(dr_engine_update(&a,0,0)!=0,-29,"update after end must fail");
    D50_Log("PASS: D53-A bt3xbx one Battle_Term per end, pad seam restored, "
            "no double release");

    /* ---- reproducibility: the same sequence twice, same hash ---------- *
     * The real engine is a singleton, so "two instances" is not available
     * here (that check lives in the reference-backend test). What matters for
     * a singleton is that replaying the same inputs reproduces the same
     * state, which is what a rematch or a desync recovery would need. */
    XbStub_Reset();
    memset(&state,0,sizeof state);
    memset(&a,0,sizeof a);
    CHECK(dr_engine_bind(&a,&dr_bt3xbx_ops,&state)==0,-30,"rebind for replay");
    CHECK(dr_engine_start(&a)==0,-31,"restart for the first run");
    CHECK(run_frames(&a,30,&hash_a)==0,-32,"first run of the sequence");
    CHECK(dr_engine_end(&a)==0,-33,"end the first run");

    XbStub_Reset();
    memset(&state,0,sizeof state);
    memset(&a,0,sizeof a);
    CHECK(dr_engine_bind(&a,&dr_bt3xbx_ops,&state)==0,-34,"rebind for replay 2");
    CHECK(dr_engine_start(&a)==0,-35,"restart for the second run");
    CHECK(run_frames(&a,30,&hash_b)==0,-36,"second run of the same sequence");
    CHECK(dr_engine_end(&a)==0,-37,"end the second run");
    CHECK(hash_a==hash_b,-38,
          "the same input sequence must reproduce the same hash");
    D50_Log("PASS: D53-A bt3xbx replaying the same 30 frames reproduces "
            "hash %08X", (unsigned)hash_a);

    /* ---- different inputs must give a different hash ------------------ *
     * Without this the replay check above would pass for a constant hash. */
    XbStub_Reset();
    memset(&state,0,sizeof state);
    memset(&a,0,sizeof a);
    CHECK(dr_engine_bind(&a,&dr_bt3xbx_ops,&state)==0,-39,"rebind for contrast");
    CHECK(dr_engine_start(&a)==0,-40,"start the contrast run");
    for(i=1;i<=30;i++)
        CHECK(dr_engine_update(&a,pattern_b((uint32_t)i),
                               pattern_a((uint32_t)i))==0,-41,
              "the contrast run must simulate");
    hash_c=dr_engine_hash(&a);
    CHECK(hash_c!=hash_a,-42,
          "a different input stream must give a different hash");
    CHECK(dr_engine_end(&a)==0,-43,"end the contrast run");
    D50_Log("PASS: D53-A bt3xbx different inputs give a different hash "
            "(%08X vs %08X)",(unsigned)hash_a,(unsigned)hash_c);

    D50_Log("PASS: D53-A bt3xbx bridge call sequence, pad injection, hash "
            "semantics and reproducibility");
    return 0;
out:
    /* Safe on a partially initialised instance, and safe to repeat. */
    (void)dr_engine_end(&a);
    return result;
}
