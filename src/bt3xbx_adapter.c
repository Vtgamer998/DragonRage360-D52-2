/* SPDX-License-Identifier: MIT
 * D53-A: bridge from the engine adapter (src/engine_adapter.h) to the real
 * bt3xbx battle engine.
 *
 * Nothing here modifies the engine. It only calls the three entry points the
 * engine already exposes and documents in
 * vendor/tenkaichi-original/include/battle/battle.h:
 *
 *     Battle_Main      running=1; Battle_Init(); Battle_Loop(); Battle_Term();
 *     Battle_Loop      while (!Battle_StepFrame()) {}
 *     Battle_StepFrame ONE frame, returns non-zero when the battle is over
 *
 * Because Battle_StepFrame exists and advances exactly one frame, driving the
 * engine from lockstep needs no change to the engine at all: start ->
 * Battle_Init, update -> Battle_StepFrame, end -> Battle_Term.
 *
 * Two properties this file depends on, both verified in D53-A:
 *
 *   1. The simulation is frame driven. The battle clock advances from a fixed
 *      tick table indexed by a frame counter (BtlClock_Tick), not from a wall
 *      clock, and Battle_UpdateWork counts frames. The only real-time wait in
 *      the frame is Gfx_EndFrame(2) (two vsyncs), which paces rendering and
 *      does not write simulation state.
 *
 *   2. Input enters through gXbPadPoll, which xb_pad.h documents as replaceable
 *      ("any other source (a test, a recording, a different xsdk call)
 *      replaces it here"). Both consoles must simulate a frame from the SAME
 *      two pad words, so the local controller is not read while online: the
 *      synchronized words are injected instead.
 *
 * SINGLETON: the engine keeps one static gBattleWork, so only one instance of
 * this backend can exist at a time. The reference backend is the one that can
 * be instantiated twice (it is what the D53-A host test does).
 *
 * Built only when DR_HAVE_BT3XBX is defined, i.e. in the XEX build where the
 * engine is linked. Host tests compile this file with a stub engine
 * (d50/bt3xbx_stub.c) to check the call sequence and the pad injection.
 */
#include "bt3xbx_adapter.h"

#ifdef DR_HAVE_BT3XBX

#include "battle/battle.h"
#include "xb_pad.h"
#include <stdint.h>
#include <string.h>

/* Battle_Init and Battle_Term come from battle.h (s32, = int). Battle_StepFrame
 * is the one entry point that header does not declare, so it is declared here
 * with the engine's own type. */
extern s32 Battle_StepFrame(void);

/* ---- pad injection ---------------------------------------------------- */
/* The engine reads the controllers once per frame through gXbPadPoll. While a
 * match is online that must return the synchronized words instead of the local
 * pad, otherwise the two consoles would simulate the same frame with different
 * inputs and diverge on the first frame. The previous poll function is saved
 * and put back in end(), so offline play is untouched. */
static int (*saved_poll)(int,XbPadState *);
static uint32_t sync_in[2];
static int seam_installed;

/* D52-2 input word -> XINPUT_GAMEPAD_* bits (the layout xb_plat_pad.c uses). */
static uint16_t bits_to_xinput(uint32_t in)
{
    uint16_t b=0;
    if(in&(1u<<0)) b|=0x0004u;   /* left  -> X_LEFT  */
    if(in&(1u<<1)) b|=0x0008u;   /* right -> X_RIGHT */
    if(in&(1u<<2)) b|=0x0001u;   /* up    -> X_UP    */
    if(in&(1u<<3)) b|=0x0002u;   /* down  -> X_DOWN  */
    if(in&(1u<<4)) b|=0x1000u;   /* A     -> X_A     */
    if(in&(1u<<5)) b|=0x2000u;   /* B     -> X_B     */
    if(in&(1u<<6)) b|=0x4000u;   /* X     -> X_X     */
    if(in&(1u<<7)) b|=0x8000u;   /* Y     -> X_Y     */
    return b;
}

static int sync_poll(int index,XbPadState *out)
{
    if(!out || index<0 || index>1) return -1;
    memset(out,0,sizeof *out);
    out->connected=1;
    out->buttons=bits_to_xinput(sync_in[index]);
    /* Sticks and triggers stay centred/zero: the D52-2 word carries the d-pad
     * and the four face buttons only. */
    return 0;
}

/* ---- hash of the real simulation state --------------------------------- *
 * FNV-1a over the gameplay fields of gBattleWork. Two rules make this correct
 * on any ABI and on both consoles:
 *
 *   - fields are read BY NAME, never by a raw byte range: BattleWork contains
 *     BattleMember, whose layout depends on pointer size, so offsetof() on the
 *     host does not match the PowerPC target. Reading m->chara gives the right
 *     offset for whatever ABI this is compiled for.
 *   - BattleMember.data and BattleMember.buf are the loader's heap pointers and
 *     are deliberately skipped: a heap address is not part of the simulation
 *     and would make the hash differ for reasons that mean nothing.
 *
 * Everything else in battle.h is scalar, and BattleResult/BattleEvents/BtlClock
 * have identical sizes on host and target (verified in D53-A). */
static uint32_t fnv_u32(uint32_t h,uint32_t v)
{
    h^= v        &0xFFu; h*=16777619u;
    h^=(v>> 8)   &0xFFu; h*=16777619u;
    h^=(v>>16)   &0xFFu; h*=16777619u;
    h^=(v>>24)   &0xFFu; h*=16777619u;
    return h;
}
static uint32_t fnv_i32(uint32_t h,int32_t v) { return fnv_u32(h,(uint32_t)v); }
static uint32_t fnv_u64(uint32_t h,uint64_t v)
{ return fnv_u32(fnv_u32(h,(uint32_t)v),(uint32_t)(v>>32)); }
/* The bit pattern, not the value: IEEE results are bit-identical for the same
 * operations on both consoles, and this avoids any float comparison. */
static uint32_t fnv_f32(uint32_t h,float v)
{ uint32_t bits; memcpy(&bits,&v,sizeof bits); return fnv_u32(h,bits); }

static uint32_t mix_member(uint32_t h,const BattleMember *m)
{
    int i;
    h=fnv_i32(h,m->chara);
    h=fnv_i32(h,m->costume);
    h=fnv_i32(h,m->variant);
    h=fnv_i32(h,m->cpuLevel);
    h=fnv_f32(h,m->health);
    for(i=0;i<8;i++)  h=fnv_u32(h,(uint32_t)m->items.id[i]);
    for(i=0;i<8;i++)  h=fnv_i32(h,m->bonus[i]);
    h=fnv_i32(h,m->aiType);
    for(i=0;i<4;i++)  h=fnv_i32(h,m->ability[i]);
    /* m->data and m->buf: loader heap pointers, skipped on purpose. */
    return h;
}

static uint32_t mix_result(uint32_t h,const BattleResult *r)
{
    int i;
    h=fnv_i32(h,r->winner);
    h=fnv_i32(h,r->reason);
    h=fnv_i32(h,r->unk8);
    h=fnv_i32(h,r->unkC);
    h=fnv_u64(h,r->eventSummary);
    h=fnv_i32(h,r->frames);
    for(i=0;i<2;i++) h=fnv_i32(h,r->unk1C[i]);
    for(i=0;i<2;i++) h=fnv_i32(h,r->unk24[i]);
    for(i=0;i<2;i++) h=fnv_f32(h,r->health[i]);
    /* BtlClock: driven by a fixed tick table per frame, not by wall time. */
    h=fnv_u32(h,r->clock.ticks);
    h=fnv_i32(h,r->clock.hours);
    h=fnv_i32(h,r->clock.minutes);
    h=fnv_i32(h,r->clock.seconds);
    h=fnv_i32(h,r->clock.ms);
    h=fnv_i32(h,r->clock.timeLeft);
    h=fnv_i32(h,r->clock.unkE);
    h=fnv_i32(h,r->unk44);
    return h;
}

static uint32_t mix_events(uint32_t h,const BattleEvents *e)
{
    int s,i;
    h=fnv_i32(h,e->script);
    h=fnv_i32(h,e->unk4);
    for(s=0;s<2;s++) {
        for(i=0;i<2;i++) h=fnv_u64(h,e->set[s].now[i]);
        for(i=0;i<2;i++) h=fnv_u64(h,e->set[s].prev[i]);
        for(i=0;i<2;i++) h=fnv_u64(h,e->set[s].held[i]);
    }
    h=fnv_i32(h,e->interrupt);
    h=fnv_i32(h,e->waitFlag);
    return h;
}

static uint32_t bt_hash(void *ctx)
{
    const BattleWork *w;
    uint32_t h=2166136261u;
    int s,m;
    (void)ctx;
    w=Battle_GetWork();
    if(!w) return 0;
    /* Match setup: rule, options and both rosters. */
    h=fnv_i32(h,w->setup.version);
    h=fnv_i32(h,w->setup.script);
    h=fnv_i32(h,w->setup.unk5A0);
    h=fnv_i32(h,w->setup.unk5A4);
    for(s=0;s<2;s++) {
        const BattleSide *side=&w->setup.sides[s];
        h=fnv_i32(h,side->memberCount);
        for(m=0;m<BTL_MEMBER_MAX;m++) h=mix_member(h,&side->members[m]);
        h=fnv_i32(h,side->lead);
        h=fnv_i32(h,side->unk1FC);
        h=fnv_i32(h,side->unk200);
        h=fnv_i32(h,side->pad);
        h=fnv_i32(h,side->control);
        h=fnv_i32(h,side->unk20C);
        h=fnv_u64(h,side->charaBits.bits[0]);
        h=fnv_u64(h,side->charaBits.bits[1]);
        h=fnv_i32(h,side->startForm.chara);
        h=fnv_i32(h,side->startForm.costume);
        h=fnv_i32(h,side->startForm.variant);
        h=fnv_i32(h,side->form.chara);
        h=fnv_i32(h,side->form.costume);
        h=fnv_i32(h,side->form.variant);
        h=fnv_i32(h,side->objId);
        h=fnv_i32(h,side->modelSlot);
    }
    /* Opponent pool of mode 3. */
    h=fnv_i32(h,w->pool.cur);
    h=fnv_i32(h,w->pool.count);
    for(m=0;m<BTL_POOL_MAX;m++) h=mix_member(h,&w->pool.members[m]);
    /* Result, events, flags, running. */
    h=mix_result(h,&w->result);
    h=mix_events(h,&w->events);
    h=fnv_u64(h,w->flags);
    h=fnv_i32(h,w->running);
    h=fnv_i32(h,w->unk19FC);
    return h;
}

/* ---- the backend ------------------------------------------------------ */

static int bt_start(void *ctx)
{
    DrBt3xbxState *s=ctx;
    if(!s || s->running) return -1;
    if(Battle_Init()<0) return -1;     /* engine refused: stay not running */
    s->running=1;
    /* Install the injection seam for the whole match and restore it at end(),
     * so a local (offline) game keeps reading the real controller. */
    if(!seam_installed) { saved_poll=gXbPadPoll; seam_installed=1; }
    gXbPadPoll=sync_poll;
    sync_in[0]=sync_in[1]=0;
    return 0;
}

static int bt_update(void *ctx,uint32_t in0,uint32_t in1)
{
    DrBt3xbxState *s=ctx;
    int rc;
    if(!s || !s->running) return -1;
    sync_in[0]=in0; sync_in[1]=in1;
    rc=Battle_StepFrame();
    if(rc<0) return -1;                /* the frame aborted (probe/instrument) */
    return rc?1:0;                     /* 0 = keep going, else battle over */
}

static int bt_end(void *ctx)
{
    DrBt3xbxState *s=ctx;
    if(!s || !s->running) return -1;
    if(seam_installed) {               /* give the real controller back */
        gXbPadPoll=saved_poll;
        seam_installed=0;
    }
    s->running=0;
    return Battle_Term()<0?-1:0;
}

const DrEngineOps dr_bt3xbx_ops = {
    "bt3xbx", bt_start, bt_update, bt_end, bt_hash
};

#else  /* !DR_HAVE_BT3XBX */

/* The engine is not linked in this build. A named object keeps the translation
 * unit non-empty without pretending a backend exists; the real table is only
 * declared in bt3xbx_adapter.h, which the XEX build includes. */
extern int dr_bt3xbx_adapter_absent;
int dr_bt3xbx_adapter_absent;

#endif
