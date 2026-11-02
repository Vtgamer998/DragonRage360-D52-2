/* SPDX-License-Identifier: MIT
 * D53-A: the real-engine backend of the engine adapter.
 *
 * Only the XEX build has the battle engine linked, so only that build may
 * include this header for real use. The host test includes it too, but links
 * d50/bt3xbx_stub.c instead of the engine.
 */
#ifndef DR_BT3XBX_ADAPTER_H
#define DR_BT3XBX_ADAPTER_H
#include "engine_adapter.h"

/* The backend's context: the caller owns it and it must outlive the adapter.
 * The engine holds one static gBattleWork, so this backend is a SINGLETON -
 * bind exactly one adapter to it. The reference backend (engine_adapter.h) is
 * the one that can be instantiated twice, which is what the D53-A two-instance
 * determinism test uses. */
typedef struct { int running; } DrBt3xbxState;

/* start = Battle_Init, update = Battle_StepFrame (one frame, with the
 * synchronized pads injected through gXbPadPoll), end = Battle_Term (which
 * also restores gXbPadPoll), hash = FNV-1a over the gameplay fields of
 * gBattleWork with BattleMember.data / .buf skipped. */
extern const DrEngineOps dr_bt3xbx_ops;

#endif
