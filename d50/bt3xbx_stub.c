/* SPDX-License-Identifier: AGPL-3.0-or-later
 * The stub battle engine: see bt3xbx_stub.h for what it is and is not. */
#include "bt3xbx_stub.h"
#include "battle/battle.h"
#include "battle/btl_char_mgr.h"   /* the roster the D53-B hash reads */
#include "battle/btl_char_ctl.h"   /* the change queue inside it */
#include "dr_rand.h"
#include "xb_pad.h"
#include <string.h>

static BattleWork gWork;
static int gInitCalls, gTermCalls, gFrames, gPollCalls;
static uint16_t gLastButtons[2];

/* D53-B: the roster and its two fighters, plus the AI and script values the
 * hash reads through the engine's getters. The real engine owns all of this;
 * here it is flat storage so a test can poke one piece at a time and watch
 * the hash move (or not). */
static BtlCharMgr gMgr;
static BtlMgrChr gFighters[2];
static s32 gAiFrame, gAiType[2], gAiLevel[2], gStory, gEventRunning;

BtlCharMgr *gBtlChars = &gMgr;

/* The change queue lives INSIDE the roster's byte array at 0x138, exactly as
 * on the console (btl_char_ctl.h), so the adapter's cast lands on real data.
 * The struct is 4 bytes wider on a 64-bit host (its `cur` member is a pointer)
 * and still fits: BtlCharMgr ends at 0x2A8 here and the queue needs 0x144 from
 * 0x160. */
static BtlChangeQueue *Stub_Queue(void)
{
    return (BtlChangeQueue *)(void *)gMgr.unk138;
}

void XbStub_Reset(void)
{
    memset(&gWork,0,sizeof gWork);
    memset(&gMgr,0,sizeof gMgr);
    memset(gFighters,0,sizeof gFighters);
    gInitCalls=gTermCalls=gFrames=gPollCalls=0;
    gLastButtons[0]=gLastButtons[1]=0;
    gAiFrame=0;
    gAiType[0]=gAiType[1]=0;
    gAiLevel[0]=gAiLevel[1]=0;
    gStory=-1;
    gEventRunning=0;
    gBtlChars=&gMgr;
}

int XbStub_InitCalls(void)   { return gInitCalls; }
int XbStub_TermCalls(void)   { return gTermCalls; }
int XbStub_Frames(void)      { return gFrames; }
int XbStub_PollCalls(void)   { return gPollCalls; }
void XbStub_LastButtons(uint16_t out[2])
{ if(out){out[0]=gLastButtons[0];out[1]=gLastButtons[1];} }

void XbStub_SetLoaderPointer(void *p)
{ gWork.setup.sides[0].members[0].data=p; }

/* ---- D53-B: one poke per block of state the hash must cover ------------ *
 * Each returns the hash's job: move it. Nothing here is restored afterwards
 * on purpose -- the next start runs Battle_Init again and zeroes it all. */
void XbStub_SetFighterAction(int fighter,int32_t action)
{ if(fighter>=0 && fighter<2) gFighters[fighter].action=action; }

void XbStub_SetRosterFrame(int32_t frame)
{ gMgr.frame=frame; }

void XbStub_SetFighterRand(uint32_t state)
{ gMgr.unk18=state; }          /* 0x18: BtlChar_Rand's state (randState) */

void XbStub_SetAiFrame(int32_t frame)
{ gAiFrame=frame; }

void XbStub_SetScript(int32_t story,int32_t event_running)
{ gStory=story; gEventRunning=event_running; }

void XbStub_SetChange(int32_t state,int32_t time_stop)
{ Stub_Queue()->state=state; Stub_Queue()->timeStop=time_stop; }

/* The queue's `cur` points INTO the queue. It is an address, not simulation:
 * the adapter must leave it out of the hash. */
void XbStub_SetChangeCur(void *p)
{ Stub_Queue()->cur=(BtlChangeReq *)p; }

/* The four entry points src/bt3xbx_adapter.c calls. Types match battle.h. */
s32 Battle_Init(void)
{
    int i;
    gInitCalls++;
    memset(&gWork,0,sizeof gWork);
    gFrames=0;
    gWork.running=1;
    /* A plausible setup: one fighter per side, and DIFFERENT loader pointers
     * on each side so that hashing them by mistake would be visible. */
    gWork.setup.version=7;
    gWork.setup.sides[0].memberCount=1;
    gWork.setup.sides[0].members[0].chara=7;
    gWork.setup.sides[0].members[0].health=100.0f;
    gWork.setup.sides[0].members[0].data=(void *)(uintptr_t)0xD00DF00Du;
    gWork.setup.sides[1].memberCount=1;
    gWork.setup.sides[1].members[0].chara=9;
    gWork.setup.sides[1].members[0].health=100.0f;
    gWork.setup.sides[1].members[0].data=(void *)(uintptr_t)0xBADBADBAu;
    gWork.pool.count=2;
    gWork.pool.members[0]=gWork.setup.sides[0].members[0];
    gWork.pool.members[1]=gWork.setup.sides[1].members[0];
    gWork.result.health[0]=100.0f;
    gWork.result.health[1]=100.0f;
    /* BtlChar_AllocAll(2) in the real engine: the roster and its fighters,
     * all zeroed, then reset. The adapter's hash walks these two. */
    memset(&gMgr,0,sizeof gMgr);
    memset(gFighters,0,sizeof gFighters);
    gMgr.count=2;
    gMgr.chars=gFighters;
    gMgr.frame=0;
    for(i=0;i<2;i++) {
        gFighters[i].side=i;
        gFighters[i].index=i;
        gFighters[i].action=0x1000+i;      /* a value the test can move away from */
        gFighters[i].members[0].chara=(i==0)?7:9;
        gFighters[i].members[0].gauge.health=10000;
    }
    /* The AI and the script come up in their initial state. */
    gAiFrame=0;
    gAiType[0]=0; gAiType[1]=1;
    gAiLevel[0]=5; gAiLevel[1]=7;
    gStory=-1;
    gEventRunning=0;
    /* Battle_Init draws a little, as the real one may: the adapter must have
     * reseeded BEFORE this, so these two draws land on a fresh state and the
     * test can count them. */
    (void)dr_rand();
    (void)dr_rand();
    return 0;
}

s32 Battle_Term(void)
{
    gTermCalls++;
    gWork.running=0;
    /* The real Battle_Term frees the roster; leaving the pointers in place
     * here would let a mistaken hash-after-end read stale memory unnoticed,
     * so the hash's phase guard is what the test relies on. */
    gBtlChars=&gMgr;
    return 0;
}

s32 Battle_StepFrame(void)
{
    XbPadState st;
    int i;
    /* Once per frame, both pads, through gXbPadPoll - the same call the
     * engine's Pad_Update makes. If the adapter failed to install its seam
     * this would read the real (absent) controller and the test would see
     * buttons == 0. */
    for(i=0;i<2;i++) {
        memset(&st,0,sizeof st);
        if(gXbPadPoll && gXbPadPoll(i,&st)==0 && st.connected) {
            gLastButtons[i]=st.buttons;
            gPollCalls++;
        }
    }
    gFrames++;
    /* State that moves with the injected pads, so the hash behaves like a
     * real fight's hash: it depends on what the two players did. */
    gWork.result.frames=gFrames;
    gWork.result.unk1C[0]+=(s32)gLastButtons[0];
    gWork.result.unk1C[1]+=(s32)gLastButtons[1];
    if(gLastButtons[0]&0x1000u) gWork.result.health[0]-=1.0f;  /* pad 0 A */
    if(gLastButtons[1]&0x1000u) gWork.result.health[1]-=1.0f;  /* pad 1 A */
    /* The engine's battle clock: a fixed tick table indexed by a frame
     * counter, never a wall clock. Mirrored here to keep the stub honest. */
    gWork.result.clock.ticks=(u32)gFrames;
    gWork.result.clock.ms=(s16)(gFrames%1000);
    gWork.flags=0x100u|(u64)gFrames;
    gWork.running=1;
    /* The roster advances too, so the hash cannot be satisfied by the battle
     * work block alone. */
    gMgr.frame=gFrames;
    gFighters[0].action=0x1000+(gFrames&0x3F);
    gFighters[1].action=0x2000+(gFrames&0x3F);
    gAiFrame=gFrames;
    return gFrames>=BT3XBX_STUB_FRAMES?1:0;
}

BattleWork *Battle_GetWork(void) { return &gWork; }

/* ---- the getters src/bt3xbx_adapter.c reads the AI and script through -- */
s32 BtlAiMgr_GetFrame(void)     { return gAiFrame; }
s32 BtlAiMgr_GetType(s32 side)  { return (side>=0&&side<2)?gAiType[side]:0; }
s32 BtlAiMgr_GetLevel(s32 side) { return (side>=0&&side<2)?gAiLevel[side]:0; }
s32 BtlScript_GetStory(void)    { return gStory; }
s32 BtlScript_IsEventRunning(void) { return gEventRunning; }
