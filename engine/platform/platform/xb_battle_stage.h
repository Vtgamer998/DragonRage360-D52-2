/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef XB_BATTLE_STAGE_H
#define XB_BATTLE_STAGE_H
#include "dr_stage_collision.h"
typedef struct XbBattleStageCollision XbBattleStageCollision;
enum {XB_STAGE_STATE=-850,XB_STAGE_MEMORY=-851};
/* Prepare a private native collision arena, including zones/neighbours,
 * static/destructible meshes and initial unbroken object state. This is NOT
 * full stage initialization: render objects, animation and physics
 * bodies are still absent. MAPD/navigation use a separate preparation step. Installation keeps the stage NOT-READY flag. */
int XbBattleStage_PrepareCollision(const void *,size_t,XbBattleStageCollision **);
int XbBattleStage_InstallCollision(XbBattleStageCollision *);
int XbBattleStage_UninstallCollision(XbBattleStageCollision *);
int XbBattleStage_FreeCollision(XbBattleStageCollision *);
/* Calls the ORIGINAL StgCol_QueryZone / ColMesh_WalkBox readers on the installed
 * native structures. Uses their unbroken/broken filtering and leaf order. */
int XbBattleStage_QueryCollision(XbBattleStageCollision *,uint32_t,
 const float min[3],const float max[3],DrStageTriangleFn,void *,int *);
/* Selected, owned MAPD start/alternative/special placements; NOT full stage.
 * Preparation must precede installation and never changes NOT_READY. */
int XbBattleStage_PrepareStarts(XbBattleStageCollision *,const void *,size_t);
/* kind 0=start, 1=alternative, 2=special. Original collision walk and original
 * StgGround_TestTri supply ground. Missing ground returns error, zero outputs. */
int XbBattleStage_GetPlacement(XbBattleStageCollision *,unsigned,unsigned,float[4],float[4],int *);
/* Native dispatch for the adapted original GetStartPlace/GetPlace. */
int XbStage_TryStartPlace(int,void *,void *,int,int *);
int XbBattleStage_PrepareParameters(XbBattleStageCollision *,const void *,size_t);
int XbStage_TryBindNav(void **);
int XbBattleStage_HasParameters(void);
typedef struct XbStageGeometryInfo {unsigned groups,materials,meshes,parts,nodes,cells,objects,timers,textures;} XbStageGeometryInfo;
int XbBattleStage_PrepareGeometry(XbBattleStageCollision *,const void *,size_t,XbStageGeometryInfo *);
int XbBattleStage_FreeGeometry(XbBattleStageCollision *);
int XbBattleStage_BeginGeometry(XbBattleStageCollision *);
int XbBattleStage_EndGeometry(XbBattleStageCollision *);
int XbStage_TryBindGeometryData(void *);
int XbStage_TryBindGeometryTexture(void *,void **);
int XbBattleStage_CheckCameras(XbBattleStageCollision *,unsigned *);
int XbBattleStage_CheckEffects(XbBattleStageCollision *,unsigned *,unsigned *);
int XbBattleStage_CheckSurfaces(XbBattleStageCollision *,unsigned *,unsigned *);
int XbBattleStage_CheckAllCameras(unsigned *,unsigned *);
#endif
