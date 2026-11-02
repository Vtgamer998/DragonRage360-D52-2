/* SPDX-License-Identifier: AGPL-3.0-or-later
 * A stand-in for the bt3xbx battle engine, used only by the host test of
 * src/bt3xbx_adapter.c. It implements the four symbols the adapter calls and
 * behaves like the engine in the two ways the adapter depends on:
 *
 *   - Battle_StepFrame reads both pads ONCE through gXbPadPoll, exactly as the
 *     engine's Pad_Update does, so a test can prove the adapter really
 *     injected the synchronized inputs instead of the local controller;
 *   - it advances one frame per call and returns 1 when the match is over.
 *
 * It is NOT the engine and proves nothing about the real fight.
 */
#ifndef DR_BT3XBX_STUB_H
#define DR_BT3XBX_STUB_H
#include <stdint.h>

/* Frames the stub battle lasts before Battle_StepFrame reports "over". */
#define BT3XBX_STUB_FRAMES 64

void XbStub_Reset(void);
int  XbStub_InitCalls(void);
int  XbStub_TermCalls(void);
int  XbStub_Frames(void);
int  XbStub_PollCalls(void);
/* The XINPUT buttons the adapter last injected, per pad. */
void XbStub_LastButtons(uint16_t out[2]);
/* Pokes BattleMember.data of side 0. The adapter must NOT hash it: a loader
 * heap pointer is not part of the simulation. */
void XbStub_SetLoaderPointer(void *p);

/* ---- D53-B: one poke per block of state the hash must cover ------------ *
 * Every one of them must MOVE the hash (except XbStub_SetChangeCur, which
 * must not: it is an address). Nothing is restored by the caller: the next
 * Battle_Init zeroes all of it again. */
void XbStub_SetFighterAction(int fighter,int32_t action); /* a fighter        */
void XbStub_SetRosterFrame(int32_t frame);                /* gBtlChars->frame */
void XbStub_SetFighterRand(uint32_t state);               /* the 2nd RNG      */
void XbStub_SetAiFrame(int32_t frame);                    /* BtlAiMgr_GetFrame*/
void XbStub_SetScript(int32_t story,int32_t event_running);/* the battle script*/
void XbStub_SetChange(int32_t state,int32_t time_stop);   /* change queue     */
void XbStub_SetChangeCur(void *p);                        /* queue's pointer  */

#endif
