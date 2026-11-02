/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef XB_BATTLE_RUNTIME_H
#define XB_BATTLE_RUNTIME_H
#include <stddef.h>
typedef struct XbBattleRuntime XbBattleRuntime;
/* Dedicated native common resources for object/roster boot. Common 3 and the
 * effect scene are not complete. A separate API attaches a texture subset
 * of common 3; other entries remain unavailable. No readiness marker is set. */
int XbBattleRuntime_Open(const void *,size_t,const void *,size_t,XbBattleRuntime **);
int XbBattleRuntime_Close(XbBattleRuntime *);
int XbBattleRuntime_HasResources(void);
int XbBattleRuntime_EnterOriginal(void);
int XbBattleRuntime_LeaveOriginal(void);
int XbBattleRuntime_BindLight(void **);
/* After original object init, models 0/1, and grounded starts: initializes a
 * temporary original save/progress, versus setup, original BtlChar and BtlAi.
 * Human side 0 / CPU side 1, character ids 0/1. Does not call Battle_Init or
 * a frame of the effect/camera/HUD scene. End before freeing models. */
int XbBattleRuntime_BeginManagers(XbBattleRuntime *);
int XbBattleRuntime_EndManagers(XbBattleRuntime *);
typedef struct XbBattleRuntimeInfo {int health[2],ki[2],ai_type[2],ai_level[2];} XbBattleRuntimeInfo;
int XbBattleRuntime_GetInfo(XbBattleRuntime *,XbBattleRuntimeInfo *);
int XbBattleRuntime_BeginAudio(XbBattleRuntime *,const void *const[4],const size_t[4]);
int XbBattleRuntime_ServiceAudio(XbBattleRuntime *);
int XbBattleRuntime_EndAudio(XbBattleRuntime *);
typedef struct XbCommon3Info {unsigned entries,textures,images,unprepared,prepared;} XbCommon3Info;
/* Texture subset only: success does not mean all entries were prepared. */
int XbBattleRuntime_AttachCommon3(XbBattleRuntime *,const void *,size_t,XbCommon3Info *);
int XbBattleRuntime_TryCommon3Entry(int,void **);
/* 0 absent, 1 TexFile, 2 emitter set, 3 parameters, 4 V000, 5 mesh, 6 configuration. */
int XbBattleRuntime_Common3Type(int);
int XbBattleRuntime_BeginStage(XbBattleRuntime *,void *stage);
int XbBattleRuntime_ServiceStage(XbBattleRuntime *);
int XbBattleRuntime_EndStage(XbBattleRuntime *);
/* Original camera and effect-scene lifecycle, after stage and fighter resources. */
int XbBattleRuntime_BeginScene(XbBattleRuntime *);
int XbBattleRuntime_BeginSceneCharacters(XbBattleRuntime *);
int XbBattleRuntime_ServiceScene(XbBattleRuntime *);
int XbBattleRuntime_EndSceneCharacters(XbBattleRuntime *);
int XbBattleRuntime_EndScene(XbBattleRuntime *);
#endif
