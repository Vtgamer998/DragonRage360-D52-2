/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef XB_BATTLE_FIGHTER_H
#define XB_BATTLE_FIGHTER_H
#include <stddef.h>
#include <stdint.h>
#include "xb_fighter_tables.h"
#include "xb_character_effects.h"
typedef struct XbBattleFighter XbBattleFighter;
typedef struct XbFighterMesh {unsigned node;const void *vif;size_t vif_bytes;float world[16],pivot_a[4],pivot_b[4];} XbFighterMesh;
typedef struct XbFighterTexture {unsigned family,index,width,height;size_t bytes;const void *rgba;} XbFighterTexture;
typedef struct XbFighterPrepared {uint32_t bones,motions,empty_motions,model_motions,empty_model_motions;} XbFighterPrepared;
typedef struct XbFighterFaceState {int eye,mode,shape;unsigned lip_index,lip_count;float lip_time,lip_end,jaw[4];} XbFighterFaceState;
/* Owned native skeleton, typed parameter/attack/ki/movement windows, BobjWork and body data;
 * decoded clips feed the original player through bounded ownership bridges.
 * Pose/matrix/volume updates are explicit. This is still not a registered
 * character: full roster tables, renderer submission and character/AI managers are
 * not initialized, and this API cannot authorize Battle_Main. */
int XbBattleFighter_PrepareModelAnimation(const void *model,size_t model_size,
 const void *motions,size_t motions_size,XbBattleFighter **out,XbFighterPrepared *);
int XbBattleFighter_AttachEffectManagers(XbBattleFighter *,const void *,size_t,XbCharacterEffectsInfo *);
void XbBattleFighter_FreeModelAnimation(XbBattleFighter *);
/* Actual original PlayModel/Step functions, operating on the owned BobjObj. */
int XbBattleFighter_PlayModel(XbBattleFighter *,unsigned index,unsigned mode);
int XbBattleFighter_StepModel(XbBattleFighter *,uint32_t *frame,uint32_t *length);
int XbBattleFighter_PlayMotion(XbBattleFighter *,unsigned motion,unsigned mode);
int XbBattleFighter_PlayRetargeted(XbBattleFighter *,XbBattleFighter *other,unsigned motion);
int XbBattleFighter_SamplePose(XbBattleFighter *);
int XbBattleFighter_GetNodePose(XbBattleFighter *,unsigned node,float pos[4],float rot[4]);
int XbBattleFighter_CalcMatrices(XbBattleFighter *);
int XbBattleFighter_GetNodeMatrix(XbBattleFighter *,unsigned node,float matrix[16]);
int XbBattleFighter_UpdateVolumes(XbBattleFighter *,unsigned *body_parts,unsigned *attack_spheres,unsigned *attack_boxes);
int XbBattleFighter_FindAttackMotion(XbBattleFighter *,unsigned *motion); /* resource inspection, not move selection AI */
/* GetMesh refuses stale/uncomputed matrices. Borrowed renderer inputs; canonical VIF remains LE. These do not submit draws. */
int XbBattleFighter_GetMesh(XbBattleFighter *,unsigned part,XbFighterMesh *);
int XbBattleFighter_GetFaceGeometry(XbBattleFighter *,unsigned shape,const void **bytes,size_t *size);
int XbBattleFighter_PrepareTextures(XbBattleFighter *,unsigned *count);
int XbBattleFighter_GetTexture(XbBattleFighter *,unsigned index,XbFighterTexture *);
/* Borrowed native reader windows; opaque tails and unreviewed fields are not combat-ready. */
int XbBattleFighter_GetNativeTable(XbBattleFighter *,unsigned,const void **,size_t *);
/* Caller supplies the real converted D_002F2420 table, never synthetic zeroes.
 * An owned copy survives caller lifetime; absence rejects table retarget. */
int XbBattleFighter_BindCommonReference(XbBattleFighter *,const float *xyz,size_t floats);
int XbBattleFighter_PlayCommonRetargeted(XbBattleFighter *,XbBattleFighter *,unsigned motion);
/* Diagnostic registry uses original free/used lists and gBtlObjTbl. It refuses
 * an existing engine work area. Published objects are INACTIVE; no character
 * or AI manager boot. Unregister/free all owners before ending the session. */
int XbBattleFighter_BeginObjectDiagnostic(void);
int XbBattleFighter_EndObjectDiagnostic(void);
int XbBattleFighter_RegisterObject(XbBattleFighter *,int *id);
int XbBattleFighter_UnregisterObject(XbBattleFighter *);
/* Runs the REAL BtlObj_Setup model lifecycle against owned native resources.
 * It activates a model object, not BtlChar/BtlAi or a battle. Must be paired
 * with DestroyModelObject (also performed by unregister/free). */
int XbBattleFighter_SetupModelObject(XbBattleFighter *,unsigned side);
int XbBattleFighter_DestroyModelObject(XbBattleFighter *);
/* Fresh original object session; reserves original fighter ids 0/1. */
int XbBattleFighter_CreateModelObject(XbBattleFighter *,unsigned side,int *id);
/* Original CPU facial state machine on an active native model. Track-number
 * lip animation only: arbitrary pointer mode 14 is deliberately unavailable.
 * No audio clock/speech synchronization or GPU presentation is implied. */
int XbBattleFighter_SetFace(XbBattleFighter *,unsigned mode,unsigned track);
int XbBattleFighter_SetEye(XbBattleFighter *,unsigned frame);
int XbBattleFighter_StepFace(XbBattleFighter *,XbFighterFaceState *);
/* Original BtlObj_Init(1)/Term session. Requires installed Runtime resources
 * and initialized game heap. End with EndObjectDiagnostic after all models
 * have been destroyed; borrowed resources never replace original buffers. */
int XbBattleFighter_BeginOriginalObjects(void);
#endif
