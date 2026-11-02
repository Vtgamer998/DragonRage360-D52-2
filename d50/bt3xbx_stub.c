/* SPDX-License-Identifier: AGPL-3.0-or-later
 * The stub battle engine: see bt3xbx_stub.h for what it is and is not. */
#include "bt3xbx_stub.h"
#include "battle/battle.h"
#include "xb_pad.h"
#include <string.h>

static BattleWork gWork;
static int gInitCalls, gTermCalls, gFrames, gPollCalls;
static uint16_t gLastButtons[2];

void XbStub_Reset(void)
{
    memset(&gWork,0,sizeof gWork);
    gInitCalls=gTermCalls=gFrames=gPollCalls=0;
    gLastButtons[0]=gLastButtons[1]=0;
}

int XbStub_InitCalls(void)   { return gInitCalls; }
int XbStub_TermCalls(void)   { return gTermCalls; }
int XbStub_Frames(void)      { return gFrames; }
int XbStub_PollCalls(void)   { return gPollCalls; }
void XbStub_LastButtons(uint16_t out[2])
{ if(out){out[0]=gLastButtons[0];out[1]=gLastButtons[1];} }

void XbStub_SetLoaderPointer(void *p)
{ gWork.setup.sides[0].members[0].data=p; }

/* The four entry points src/bt3xbx_adapter.c calls. Types match battle.h. */
s32 Battle_Init(void)
{
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
    return 0;
}

s32 Battle_Term(void)
{
    gTermCalls++;
    gWork.running=0;
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
    return gFrames>=BT3XBX_STUB_FRAMES?1:0;
}

BattleWork *Battle_GetWork(void) { return &gWork; }
