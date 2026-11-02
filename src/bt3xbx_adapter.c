/* SPDX-License-Identifier: MIT
 * D53-A/D53-B: bridge from the engine adapter (src/engine_adapter.h) to the
 * real bt3xbx battle engine.
 *
 * D53-B added the two things a lockstep match needs beyond the call sequence:
 * the battle starts from a fresh random state (see bt_start) and the hash
 * covers the roster, the fighters, the AI, the script and that random state
 * (see bt_hash), so a divergence shows up as a mismatch instead of drifting.
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
#include "battle/btl_char_mgr.h"   /* the roster gBtlChars and the fighters */
#include "battle/btl_char_ctl.h"   /* the change queue living inside it */
#include "battle/btl_ai_mgr.h"     /* the CPU opponent's frame / type / level */
#include "battle/btl_script.h"     /* the battle script's story and events */
#include "dr_rand.h"
#include "xb_pad.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---- layout contract -------------------------------------------------- *
 * The header documents EVERY byte of a fighter with an offset, so the block
 * is contiguous (no padding) and the only pointer in it is objTbl at the
 * very end. Hashing the bytes up to objTbl therefore covers pose, action,
 * members, gauges, timers and flags without ever reading a pointer.
 *
 * The asserts below state RELATIONS, not absolute offsets, on purpose. The
 * PS2 offsets in the comments (pose at 0x10, objTbl at 0x15E8 ...) assume
 * u64 is eight bytes, but types.h typedefs u64 as `unsigned long`, which is
 * FOUR bytes on the 32-bit PowerPC target and eight on the LP64 host. So the
 * absolute numbers differ between the two ABIs -- and differ from the
 * comments -- while every relation between neighbouring fields holds in both.
 * What the hash needs is exactly those relations: that the fields are laid
 * out back to back and that objTbl is the first pointer. If the header ever
 * reorders or pads the block, these fail first. */
_Static_assert(offsetof(BtlMgrChr,pose)==4*sizeof(s32),
               "the four header words must precede the pose");
_Static_assert(offsetof(BtlMgrChr,prevPose)==
                   offsetof(BtlMgrChr,pose)+sizeof(BtlMgrPose),
               "pose and prevPose must be contiguous");
/* padFlagA -> unk15D4[5] -> objTbl, back to back with no gap: this is what
 * keeps the hashed range [0, objTbl) a single contiguous pointer-free block
 * on both ABIs (it holds on the target and on the LP64 host alike). */
_Static_assert(offsetof(BtlMgrChr,objTbl)==
                   offsetof(BtlMgrChr,padFlagA)+6*sizeof(s32),
               "the scalar tail must run straight into objTbl");
_Static_assert(sizeof(BtlMgrChr)>=offsetof(BtlMgrChr,objTbl)+3*sizeof(void*),
               "objTbl must fit inside the fighter");
_Static_assert(sizeof(BtlMgrPose)%4==0,
               "the pose block must stay word-aligned for the byte hash");
/* The change queue is read field by field because its `cur` member points
 * into the queue itself. Every field before it is a 32-bit word, so the
 * layout agrees on both ABIs: eight requests, then the pointer. */
_Static_assert(offsetof(BtlChangeReq,slot)==8*sizeof(s32),
               "slot must be the ninth word of a request");
_Static_assert(offsetof(BtlChangeQueue,cur)==BTL_CHANGE_RING*sizeof(BtlChangeReq),
               "the pointer must follow the eight requests");
_Static_assert(BTL_CHANGE_RING==8,"change ring size changed");

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
 * FNV-1a over everything the fight can move, in four blocks:
 *
 *   1. gBattleWork  match setup, rosters, pool, result, events, flags.
 *   2. the roster   gBtlChars: frame counters, the fighters' own random
 *                   generator, clash state, sound requests, the character
 *                   change queue, and every fighter's bytes up to the first
 *                   pointer.
 *   3. AI and script  BtlAiMgr frame/type/level, BtlScript story and events:
 *                   state that lives outside gBattleWork and outside the
 *                   roster, and that a mismatch here would otherwise hide.
 *   4. the libc RNG state  dr_rand_state(): the generator the game draws from
 *                   363 times. Its state is a pure function of how many draws
 *                   happened, so a divergence in the number of draws shows up
 *                   here on the very frame it happens.
 *
 * Two rules make this correct on any ABI and on both consoles:
 *
 *   - fields are read BY NAME, never by a raw byte range of a struct with
 *     pointers: BattleWork contains BattleMember, whose layout depends on
 *     pointer size, so offsetof() on the host does not match the PowerPC
 *     target. Reading m->chara gives the right offset for whatever ABI this
 *     is compiled for. The one raw range used (a fighter) is safe because
 *     btl_char_mgr.h documents it as pointer-free up to objTbl, asserted
 *     above.
 *   - BattleMember.data and BattleMember.buf are the loader's heap pointers
 *     and are deliberately skipped, and so are gBtlChars->chars / unk8 / unkC
 *     / tbl[] and the change queue's `cur`: a heap address is not part of the
 *     simulation and would make the hash differ for reasons that mean
 *     nothing. (On the console the image is loaded at a random address, so
 *     an address would differ between two machines every single time.)
 *
 * The byte ranges are hashed in memory order, so a host hash is not the
 * number the console computes (the two ABIs disagree on byte order). That is
 * fine: hashes are only ever compared between two builds of the SAME binary,
 * which is how the two consoles run.
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
/* A block of bytes, in memory order. Used only where the block is known to
 * hold no pointer (see the layout contract above). */
static uint32_t fnv_bytes(uint32_t h,const void *p,size_t n)
{
    const unsigned char *b=(const unsigned char *)p;
    size_t i;
    for(i=0;i<n;i++) { h^=b[i]; h*=16777619u; }
    return h;
}

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

/* How many fighters the roster may hold when it is hashed. Battle_Init passes
 * 2; the cap only keeps a corrupt count from walking off the allocation. */
#define BT_FIGHTERS_MAX 8

/* The roster (gBtlChars) and every fighter it owns.
 *
 * The roster is described by SEVERAL headers at once -- btl_char_mgr.h,
 * btl_char_get.h, btl_char_flag.h, btl_char_ctl.h ... all talk about the same
 * block, and the engine's own sources include whichever view they need. This
 * file uses btl_char_mgr.h for the block and btl_char_ctl.h for the change
 * queue inside it (the engine does the same across translation units); both
 * are included here because neither of them declares gBtlChars twice. */
static uint32_t mix_roster(uint32_t h)
{
    const BtlCharMgr *c=gBtlChars;
    const BtlChangeQueue *q;
    int i,n;
    if(!c) return h;                       /* before Battle_Init / after Term */
    h=fnv_i32(h,c->count);
    h=fnv_i32(h,c->flags);
    h=fnv_i32(h,c->frame);                 /* frames simulated since reset */
    /* 0x18 is the FIGHTERS' random generator: btl_char_get.h names it
     * randState and BtlChar_Rand advances it (state * 714025 + 4096) % 150889
     * while time is not stopped. A second RNG, separate from the libc one,
     * and just as capable of desynchronizing two consoles. 0x1C is the stage
     * timer (btl_char_get.h) that counts down every frame. */
    h=fnv_u32(h,c->unk18);
    h=fnv_i32(h,c->unk1C);
    h=fnv_u64(h,c->unk130);                /* replay viewer (btl_replay.h) */
    /* 0x40..0xA0 is the clash state (btl_char_flag.h: clashState, clashA,
     * clashB, clashC) and 0xA0..0x130 the four positional sound requests and
     * their count. Both ranges are scalars only -- verified against the view
     * that names them -- so the bytes can be hashed as they are. */
    h=fnv_bytes(h,c->unk40,sizeof c->unk40);
    h=fnv_bytes(h,c->unkA0,sizeof c->unkA0);
    /* The character change queue at 0x138 (transformations, fusions, member
     * swaps, extra objects, and the "time stopped" word at +0x274). Read
     * member by member: `cur` points INTO this queue and must stay out. */
    q=(const BtlChangeQueue *)(const void *)c->unk138;
    for(i=0;i<BTL_CHANGE_RING;i++) {
        const BtlChangeReq *r=&q->req[i];
        h=fnv_i32(h,r->player);
        h=fnv_i32(h,r->kind);
        h=fnv_i32(h,r->chara);
        h=fnv_i32(h,r->costume);
        h=fnv_i32(h,r->variant);
        h=fnv_i32(h,r->animChara);
        h=fnv_i32(h,r->unk18);
        h=fnv_i32(h,r->voiceChara);
        h=fnv_i32(h,r->slot);
    }
    h=fnv_i32(h,q->write);
    h=fnv_i32(h,q->read);
    h=fnv_i32(h,q->state);
    h=fnv_i32(h,q->loaded);
    h=fnv_i32(h,q->unk134);
    h=fnv_i32(h,q->unk138);
    h=fnv_i32(h,q->timeStop);
    /* c->unk38 and c->unk278 are the only two regions no view names, and
     * c->chars / c->unk8 / c->unkC / c->tbl[] are pointers. All left out on
     * purpose: nothing is gained by hashing an unnamed word or an address.
     *
     * The fighters: the whole documented block up to the first pointer
     * (objTbl), so pose, action, members, gauges, freeze timers and per
     * frame flags are all in. The engine allocates count * 0x1600 and
     * indexes with &chars[i], i.e. with sizeof(BtlMgrChr), so stepping by
     * sizeof() walks exactly the engine's own stride. The allocation is
     * larger than the struct in this build (u64 is four bytes here), which
     * only wastes the tail of each slot; it never reads past it. */
    n=c->count;
    if(n<0) n=0;
    if(n>BT_FIGHTERS_MAX) n=BT_FIGHTERS_MAX;
    if(!c->chars) return h;
    for(i=0;i<n;i++) h=fnv_bytes(h,&c->chars[i],offsetof(BtlMgrChr,objTbl));
    return h;
}

/* The CPU opponent and the battle script: state that lives in its own blocks
 * (gBtlAi is a heap block, gBtlScript a static one), reachable only through
 * getters because those blocks are full of pointers. BtlAiMgr_GetFrame is the
 * number of frames the AI has run, so it also catches "the AI ran a different
 * number of times", which nothing else in the hash would. */
static uint32_t mix_ai_script(uint32_t h)
{
    int s;
    h=fnv_i32(h,BtlAiMgr_GetFrame());
    for(s=0;s<2;s++) {
        h=fnv_i32(h,BtlAiMgr_GetType(s));
        h=fnv_i32(h,BtlAiMgr_GetLevel(s));
    }
    h=fnv_i32(h,BtlScript_GetStory());
    h=fnv_i32(h,BtlScript_IsEventRunning());
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
    /* The rest of the simulation: roster and fighters, AI and script, and the
     * libc generator's state (the count of draws it has served). */
    h=mix_roster(h);
    h=mix_ai_script(h);
    h=fnv_u64(h,dr_rand_state());
    return h;
}

/* ---- the backend ------------------------------------------------------ */

static int bt_start(void *ctx)
{
    DrBt3xbxState *s=ctx;
    if(!s || s->running) return -1;
    /* D53-B: the generator starts every battle from the same state.
     *
     * The menus draw with rand() (menu_s_d.c, menu_h_b.c, menu_t_b.c,
     * menu_k_c.c) and the two consoles navigate them independently, so the
     * state Battle_Init would inherit is different on each side and the very
     * first frame would already disagree. Reseeding here, before Battle_Init,
     * makes both battles begin from newlib's fresh state (1) and puts every
     * subsequent draw under the hash, where a divergence is visible. It
     * changes which random BGM/cosmetic the fight picks compared with offline
     * play, and only for this online path. */
    dr_rand_reset();
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
