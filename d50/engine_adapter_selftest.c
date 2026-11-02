/* SPDX-License-Identifier: AGPL-3.0-or-later
 * D53-A: engine adapter self-test.
 *
 * It proves the adapter contract without the Xbox toolchain, the GPU or the
 * ISO data, using the reference backend:
 *
 *   1. lifecycle safety - an out-of-order call (update before start, start
 *      while running, end twice, update after end) is a reported error and
 *      the backend is NOT called, so the engine can never be stepped before
 *      Battle_Init or released twice;
 *   2. a failed start() leaves the instance startable, and a failed update()
 *      still allows end(), so resources are always releasable;
 *   3. the completion criterion of D53-A: TWO instances fed the same input
 *      sequence produce IDENTICAL hashes at every frame;
 *   4. two instances fed DIFFERENT inputs produce different hashes, which is
 *      what makes the hash able to detect a desync at all;
 *   5. the harness drives an adapter through lockstep and both sides agree.
 *
 * It does NOT prove that the real bt3xbx engine is deterministic, that it
 * links, or that it runs on a console: that is D53-B.
 */
#include "engine_adapter_selftest.h"
#include "engine_adapter.h"
#include "online_battle.h"
#include "lobby.h"
#include "xsdk_compat.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CHECK(cond,code,msg) do { \
    if(!(cond)) { D50_Log("FAIL: %s",(msg)); result=(code); goto out; } \
} while(0)

/* ---- backends used only by this test ---------------------------------- */

/* Counts the calls, so a refused call can be proven not to reach the backend. */
typedef struct { int starts, updates, ends; int fail_start, fail_update; }
    SpyState;

static int spy_start(void *ctx)
{
    SpyState *s=ctx;
    s->starts++;
    return s->fail_start?-1:0;
}
static int spy_update(void *ctx,uint32_t in0,uint32_t in1)
{
    SpyState *s=ctx;
    (void)in0;(void)in1;
    s->updates++;
    return s->fail_update?-1:0;
}
static int spy_end(void *ctx)
{
    SpyState *s=ctx;
    s->ends++;
    return 0;
}
static uint32_t spy_hash(void *ctx)
{
    const SpyState *s=ctx;
    return 0xC0FFEE00u|(uint32_t)(s->starts*16+s->updates*4+s->ends);
}
static const DrEngineOps ops_spy={"spy",spy_start,spy_update,spy_end,spy_hash};

/* A backend missing a function: bind must refuse it. */
static const DrEngineOps ops_no_end={"no-end",spy_start,spy_update,0,spy_hash};

/* Deterministic pad pattern per side, so the run is repeatable. */
static uint32_t pattern_a(uint32_t frame)
{
    uint32_t in=0;
    if((frame/3u)%2u) in|=1u<<0; else in|=1u<<1;
    if((frame/5u)%2u) in|=1u<<4;
    return in;
}
static uint32_t pattern_b(uint32_t frame)
{
    uint32_t in=0;
    if((frame/2u)%2u) in|=1u<<2; else in|=1u<<3;
    if((frame/7u)%2u) in|=1u<<1;
    return in;
}

static uint16_t bound_port(int fd)
{
    struct sockaddr_in ad; socklen_t sz=sizeof ad;
    if(fd<0 || getsockname(fd,(struct sockaddr *)&ad,&sz)) return 0;
    return ntohs(ad.sin_port);
}

int D53_EngineAdapterSelfTest(void)
{
    DrEngineAdapter a,b;
    SpyState spy;
    DrReferenceState ra,rb;
    DrLobby host,client;
    DrOnlineBattle s0,s1;
    uint32_t hash_a=0,hash_b=0;
    uint32_t clock=1000;
    uint16_t port=0;
    int result=0;
    int i,paired=0;

    memset(&a,0,sizeof a); memset(&b,0,sizeof b);
    memset(&spy,0,sizeof spy);
    memset(&ra,0,sizeof ra); memset(&rb,0,sizeof rb);
    dr_lobby_init(&host); dr_lobby_init(&client);
    memset(&s0,0,sizeof s0); memset(&s1,0,sizeof s1);

    D50_Log("D53-A selftest: adapter contract only; the real engine needs D53-B");

    /* ---- 1. bind guards ---------------------------------------------- */
    CHECK(dr_engine_bind(&a,0,&spy)!=0,-1,"bind with NULL ops must fail");
    CHECK(dr_engine_phase(&a)==DR_ENGINE_UNBOUND,-2,
          "a failed bind must leave the instance UNBOUND");
    CHECK(dr_engine_bind(&a,&ops_no_end,&spy)!=0,-3,
          "bind must refuse a backend missing end()");
    CHECK(dr_engine_phase(&a)==DR_ENGINE_UNBOUND,-4,
          "a refused bind must not leave the instance usable");
    CHECK(dr_engine_bind(&a,&ops_spy,0)!=0,-5,"bind with NULL ctx must fail");
    CHECK(dr_engine_bind(&a,&ops_spy,&spy)==0,-6,"valid bind must succeed");
    CHECK(dr_engine_phase(&a)==DR_ENGINE_IDLE,-7,"a valid bind must be IDLE");
    D50_Log("PASS: D53-A bind refuses NULL ops, a missing end() and NULL ctx");

    /* ---- 2. calls before start must not reach the backend -------------- */
    CHECK(dr_engine_update(&a,1,2)!=0,-8,"update before start must fail");
    /* The error must be read right after the rejected call: the next one
     * overwrites it, and that is intended (it describes the last refusal). */
    CHECK(strcmp(dr_engine_error(&a),"update: engine is not running")==0,-12,
          "error text must name the rejected call");
    CHECK(dr_engine_end(&a)!=0,-9,"end before start must fail");
    CHECK(strcmp(dr_engine_error(&a),"end: engine is not running")==0,-71,
          "the end refusal must be named too");
    CHECK(spy.starts==0 && spy.updates==0 && spy.ends==0,-10,
          "a refused call must not reach the backend");
    CHECK(dr_engine_hash(&a)==0,-11,"hash while IDLE must be 0");
    D50_Log("PASS: D53-A update/end refused before start, backend untouched");

    /* ---- 3. the running sequence, and double calls -------------------- */
    CHECK(dr_engine_start(&a)==0,-13,"start must succeed");
    CHECK(dr_engine_phase(&a)==DR_ENGINE_RUNNING,-14,"start must be RUNNING");
    CHECK(spy.starts==1,-15,"start must reach the backend exactly once");
    CHECK(dr_engine_start(&a)!=0,-16,"a second start must fail");
    CHECK(spy.starts==1,-17,"a refused second start must not reach the backend");
    for(i=0;i<5;i++)
        CHECK(dr_engine_update(&a,(uint32_t)i,0)==0,-18,
              "update while running must succeed");
    CHECK(dr_engine_hash(&a)!=0,-19,"hash while RUNNING must be non-zero");
    CHECK(dr_engine_end(&a)==0,-20,"end must succeed");
    CHECK(dr_engine_phase(&a)==DR_ENGINE_ENDED,-21,"end must be ENDED");
    CHECK(spy.ends==1,-22,"end must reach the backend exactly once");
    CHECK(dr_engine_end(&a)!=0,-23,"a second end must fail (no double release)");
    CHECK(spy.ends==1,-24,"a refused second end must not reach the backend");
    CHECK(dr_engine_update(&a,1,1)!=0,-25,"update after end must fail");
    CHECK(spy.updates==5,-26,
          "only the five allowed updates must reach the backend");
    D50_Log("PASS: D53-A start/update/end sequence, no double start, no double end");

    /* ---- 4. restart after end ---------------------------------------- */
    CHECK(dr_engine_start(&a)==0,-27,"start after end must be allowed");
    CHECK(dr_engine_phase(&a)==DR_ENGINE_RUNNING,-28,"a restart must be RUNNING");
    CHECK(spy.starts==2,-29,"the restart must reach the backend");
    CHECK(dr_engine_end(&a)==0,-30,"the restarted instance must end cleanly");
    D50_Log("PASS: D53-A restart after end works (rematch path)");

    /* ---- 5. a failed start stays startable --------------------------- */
    memset(&spy,0,sizeof spy);
    spy.fail_start=1;
    CHECK(dr_engine_bind(&a,&ops_spy,&spy)==0,-31,"bind for the failure case");
    CHECK(dr_engine_start(&a)!=0,-32,"a backend that refuses start must fail");
    CHECK(dr_engine_phase(&a)==DR_ENGINE_IDLE,-33,
          "a failed start must leave the instance startable");
    spy.fail_start=0;
    CHECK(dr_engine_start(&a)==0,-34,"the retry after a failed start must work");
    CHECK(dr_engine_end(&a)==0,-35,"and it must end cleanly");
    D50_Log("PASS: D53-A failed start leaves the instance startable");

    /* ---- 6. a failed update still allows end ------------------------- */
    memset(&spy,0,sizeof spy);
    spy.fail_update=1;
    CHECK(dr_engine_bind(&a,&ops_spy,&spy)==0,-36,"bind for the update failure");
    CHECK(dr_engine_start(&a)==0,-37,"start before the failing update");
    CHECK(dr_engine_update(&a,0,0)!=0,-38,"a backend error must be reported");
    CHECK(dr_engine_phase(&a)==DR_ENGINE_RUNNING,-39,
          "a failed update must keep the instance endable");
    CHECK(dr_engine_end(&a)==0,-40,
          "end after a failed update must still release the engine");
    CHECK(spy.ends==1,-41,"and it must reach the backend exactly once");
    D50_Log("PASS: D53-A failed update still allows a safe end");

    /* ---- 7. THE CRITERION: two instances, same inputs, same hash ------ */
    dr_reference_reset(&ra);
    dr_reference_reset(&rb);
    CHECK(dr_engine_bind(&a,&dr_reference_ops,&ra)==0,-42,"bind instance A");
    CHECK(dr_engine_bind(&b,&dr_reference_ops,&rb)==0,-43,"bind instance B");
    CHECK(dr_engine_start(&a)==0 && dr_engine_start(&b)==0,-44,
          "both instances must start");
    for(i=1;i<=300;i++) {
        /* Same sequence, two separate instances: exactly what two consoles
         * do when they receive the same inputs. */
        CHECK(dr_engine_update(&a,pattern_a((uint32_t)i),
                               pattern_b((uint32_t)i))==0,-45,
              "instance A must simulate every frame");
        CHECK(dr_engine_update(&b,pattern_a((uint32_t)i),
                               pattern_b((uint32_t)i))==0,-46,
              "instance B must simulate every frame");
        hash_a=dr_engine_hash(&a);
        hash_b=dr_engine_hash(&b);
        CHECK(hash_a!=0,-47,"the hash must be non-zero while running");
        if(hash_a!=hash_b) {
            D50_Log("FAIL: D53-A two instances diverged at frame %d: %08X vs %08X",
                    i,(unsigned)hash_a,(unsigned)hash_b);
            result=-48;goto out;
        }
    }
    CHECK(hash_a==hash_b,-49,"after 300 frames the two instances must agree");
    CHECK(dr_engine_end(&a)==0 && dr_engine_end(&b)==0,-50,
          "both instances must end cleanly");
    D50_Log("PASS: D53-A two instances, same 300-frame sequence, identical hash %08X",
            (unsigned)hash_a);

    /* ---- 8. different inputs MUST give a different hash ---------------- *
     * Without this the previous check would be vacuous: a constant hash
     * would also make two instances "agree". */
    dr_reference_reset(&ra);
    dr_reference_reset(&rb);
    CHECK(dr_engine_bind(&a,&dr_reference_ops,&ra)==0,-51,"rebind A");
    CHECK(dr_engine_bind(&b,&dr_reference_ops,&rb)==0,-52,"rebind B");
    CHECK(dr_engine_start(&a)==0 && dr_engine_start(&b)==0,-53,"restart both");
    for(i=1;i<=300;i++) {
        CHECK(dr_engine_update(&a,pattern_a((uint32_t)i),
                               pattern_b((uint32_t)i))==0,-54,"A must simulate");
        /* B walks a different input stream from the very first frame. */
        CHECK(dr_engine_update(&b,pattern_b((uint32_t)i),
                               pattern_a((uint32_t)i))==0,-55,"B must simulate");
        if(i>=8 && dr_engine_hash(&a)==dr_engine_hash(&b)) {
            D50_Log("FAIL: D53-A different inputs gave the same hash at frame %d (%08X)",
                    i,(unsigned)dr_engine_hash(&a));
            result=-56;goto out;
        }
    }
    CHECK(dr_engine_hash(&a)!=dr_engine_hash(&b),-57,
          "different input streams must end with different hashes");
    CHECK(dr_engine_end(&a)==0 && dr_engine_end(&b)==0,-58,"end both");
    D50_Log("PASS: D53-A different inputs produce different hashes "
            "(the hash is not constant)");

    /* ---- 9. the harness drives an adapter through lockstep ------------ *
     * Two DrOnlineBattle instances over two real UDP lobbies: the D53-A view
     * of what D52-2 already proves, now going through the adapter. */
    if(!dr_lobby_start(&host,1,NULL,0,clock,0x11111111u)) {
        D50_Log("FAIL: D53-A host lobby start: %s",host.message);
        result=-59;goto out;
    }
    port=bound_port(host.fd);
    if(!port || !dr_lobby_start(&client,0,"127.0.0.1",port,clock,0x22222222u)) {
        D50_Log("FAIL: D53-A client lobby start: %s",client.message);
        result=-60;goto out;
    }
    for(i=0;i<300 && !paired;i++) {
        clock+=10;
        dr_lobby_tick(&client,clock);
        dr_lobby_tick(&host,clock);
        if(host.state==LOBBY_PAIRED && client.state==LOBBY_PAIRED) paired=1;
    }
    CHECK(paired,-61,"the two lobbies must pair");
    CHECK(dr_online_battle_init(&s0,&host,0)==1,-62,"bind side 0");
    CHECK(dr_online_battle_init(&s1,&client,1)==1,-63,"bind side 1");
    CHECK(strcmp(dr_online_battle_backend(&s0),"reference")==0,-64,
          "the default backend must be the reference one");

    for(i=0;i<400;i++) {
        uint32_t r0=0,r1=0;
        dr_lobby_tick(&host,clock);dr_lobby_tick(&client,clock);
        clock+=10;
        (void)dr_online_battle_update(&s0,
            pattern_a(dr_online_battle_frame(&s0)),&r0);
        (void)dr_online_battle_update(&s1,
            pattern_b(dr_online_battle_frame(&s1)),&r1);
        dr_lobby_tick(&host,clock);dr_lobby_tick(&client,clock);
        clock+=10;
        (void)dr_online_battle_update(&s0,
            pattern_a(dr_online_battle_frame(&s0)),&r0);
        (void)dr_online_battle_update(&s1,
            pattern_b(dr_online_battle_frame(&s1)),&r1);
    }
    /* Converge: lockstep legitimately sits one frame apart while in flight. */
    for(i=0;i<200 &&
        dr_online_battle_frame(&s0)!=dr_online_battle_frame(&s1);i++) {
        uint32_t r0=0,r1=0;
        dr_lobby_tick(&host,clock);dr_lobby_tick(&client,clock);
        clock+=10;
        (void)dr_online_battle_update(&s0,
            pattern_a(dr_online_battle_frame(&s0)),&r0);
        (void)dr_online_battle_update(&s1,
            pattern_b(dr_online_battle_frame(&s1)),&r1);
    }
    CHECK(dr_online_battle_frame(&s0)==dr_online_battle_frame(&s1),-65,
          "the two harness instances must reach the same frame");
    CHECK(dr_online_battle_frame(&s0)>100,-66,
          "the harness must have simulated a meaningful number of frames");
    CHECK(dr_online_battle_get_state_hash(&s0)==
          dr_online_battle_get_state_hash(&s1),-67,
          "the two harness instances must hash the same state");
    CHECK(!dr_online_battle_desync(&s0,0,0,0) &&
          !dr_online_battle_desync(&s1,0,0,0),-68,
          "no desync may be reported");
    CHECK(dr_online_battle_matched(&s0)>0 && dr_online_battle_matched(&s1)>0,
          -69,"peer hashes must actually have been compared");
    D50_Log("PASS: D53-A harness lockstep through the adapter at frame %u, "
            "hash %08X, %u peer hashes verified",
            (unsigned)dr_online_battle_frame(&s0),
            (unsigned)dr_online_battle_get_state_hash(&s0),
            (unsigned)dr_online_battle_matched(&s0));
    D50_Log("PASS: D53-A engine adapter lifecycle, determinism and lockstep");
    result=0;

out:
    /* Every path leaves through here: shutdown must be safe on a partially
     * initialised instance and safe to repeat. */
    dr_online_battle_shutdown(&s0);
    dr_online_battle_shutdown(&s1);
    dr_online_battle_shutdown(&s0);
    dr_lobby_stop(&host);
    dr_lobby_stop(&client);
    return result;
}
