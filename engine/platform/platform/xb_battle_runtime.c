/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "port_compat.h"
#undef malloc
#include "xb_battle_runtime.h"
#include "xb_battle_boot.h"
#include "xb_battle_common.h"
#include "xb_battle_stage.h"
#include "xb_model_native.h"
#include "xb_mem.h"
#include "xb_heap.h"
#include "xb_sfx.h"
#include "xb_adxt.h"
#include "sys/snd.h"
#include "battle/eft_det_b.h"
#include "dr_container.h"
#include "battle/btl_obj.h"
#include "battle/btl_char_mgr.h"
#include "battle/btl_ai_think.h"
#include "battle/battle_setup.h"
#include "battle/view_a.h"
#include "sys/common.h"
#include "sys/save.h"
#include "sys/heap.h"
#include "sys/pad.h"
#include <stdint.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>

struct XbBattleRuntime {XbBattleCommon *common;CommonRes view;void *file,*light;int managers,audio,files,scene,scene_chars;XbBattleStageCollision *stage_geometry;XbSoundBank *banks[4];void **common3;unsigned common3_count,common3_textures,common3_images,common3_prepared;uint8_t common3_type[638];};
static XbBattleRuntime *active;
static XbBattleBootGate boot_gate;
extern AiThMgr *gBtlAi;
#include "sys/job.h"
extern s32 gGscActive;
extern ViewProgress *gProgress;
extern SndMgr *gSndMgr;
extern SndRpc *gSndRpc;
extern void BtlChar_AllocAll(s32);
extern void BtlChar_FreeAll(void);
extern void BtlAiMgr_Init(void);
extern void BtlAiMgr_Term(void);
extern void Battle_ClearWork(void);

int XbBattleRuntime_HasResources(void){return active && gCommonRes==&active->view;}
int XbBattleRuntime_EnterOriginal(void){
 if(active || gCommonRes || gBtlObjWork || gBtlChars || gBtlAi || gSaveData || gProgress || gSndMgr || gSndRpc || gJobQueue || gGscActive)return XB_BATTLE_BOOT_BUSY;
 return XbBattleBoot_Enter(&boot_gate,XB_BATTLE_BOOT_ORIGINAL);
}
int XbBattleRuntime_LeaveOriginal(void){
 if(active || gCommonRes || gBtlObjWork || gBtlChars || gBtlAi || gSaveData || gProgress || gSndMgr || gSndRpc || gJobQueue || gGscActive)return XB_BATTLE_BOOT_BUSY;
 return XbBattleBoot_Leave(&boot_gate,XB_BATTLE_BOOT_ORIGINAL);
}
int XbBattleRuntime_BindLight(void **out){
 if(!XbBattleRuntime_HasResources())return 0;
 *out=active->light;return 1;
}
int XbBattleRuntime_Open(const void *common2,size_t n2,const void *common4,size_t n4,XbBattleRuntime **out){
 if(!out)return -890;
 *out=NULL;if(sizeof(void *)!=4 || !XbBattleBoot_ReadyForOriginal(&boot_gate) || active || gCommonRes || gBtlObjWork || gBtlChars || gBtlAi || gSaveData || gProgress)return -891;
 XbBattleRuntime *r=calloc(1,sizeof(*r));if(!r)return -851;
 XbBattleCommonInfo info;int e=XbBattleCommon_Prepare(common2,n2,&r->common,&info);if(e)goto fail;
 e=XbBattleCommon_AttachItems(r->common,common4,n4);if(e)goto fail;
 unsigned nc,ni;size_t bytes;e=XbBattleCommon_GetItems(r->common,&r->view.data[2],&bytes,&nc,&ni);if(e)goto fail;
 if(nc!=161 || ni<350){e=-890;goto fail;}r->view.size[2]=(s32)bytes;
 size_t offset,length;unsigned flags;e=dr_pack_memory_entry(common2,n2,0,&offset,&length,&flags);if(e || flags){e=-890;goto fail;}
 e=XbNativeTexture_Prepare((const uint8_t *)common2+offset,length,&r->light);if(e)goto fail;
 /* Native offset-table wrapper: original roster/AI consumers receive typed
  * sections. All unprepared common-2 entries remain empty, never a raw dump. */
 const void *sections[213]={0};size_t sizes[213]={0};
 static const unsigned index[6]={2,4,5,6,7,8};
 for(unsigned i=0;i<6;i++){e=XbBattleCommon_GetRosterTable(r->common,i,&sections[index[i]],&sizes[index[i]]);if(e)goto fail;}
 void *ai=NULL;e=XbBattleCommon_GetAiData(r->common,&ai,&sizes[3]);if(e)goto fail;sections[3]=ai;
 size_t total=215*4;
 for(unsigned i=0;i<213;i++){if(sizes[i]&3){e=-890;goto fail;}total+=sizes[i];}
 uint8_t *file=calloc(1,total);if(!file){e=-851;goto fail;}r->file=file;
 uint32_t *header=(uint32_t *)file;header[0]=213;size_t at=215*4;
 for(unsigned i=0;i<213;i++){header[i+1]=(uint32_t)at;if(sizes[i])memcpy(file+at,sections[i],sizes[i]);at+=sizes[i];}
 header[214]=(uint32_t)at;r->view.data[0]=file;r->view.size[0]=(s32)total;
 /* Heap_Init is idempotent in the native engine and never erases live data. */
 if(XbMem_InitHeap()){e=-851;goto fail;}Heap_Init();if(!XbHeap_Ready()){e=-851;goto fail;}
 if(XbBattleBoot_Enter(&boot_gate,XB_BATTLE_BOOT_DIAGNOSTIC)){e=-891;goto fail;}
 active=r;gCommonRes=&r->view;*out=r;return 0;
fail:
 XbNativeTexture_Free(r->light);free(r->file);XbBattleCommon_Free(r->common);free(r);return e;
}
int XbBattleRuntime_Close(XbBattleRuntime *r){
 if(!r || active!=r || gCommonRes!=&r->view || r->managers || gBtlObjWork || gBtlChars || gBtlAi || gSaveData || gProgress)return -891;
 if(XbBattleBoot_Leave(&boot_gate,XB_BATTLE_BOOT_DIAGNOSTIC))return -891;
 gCommonRes=NULL;active=NULL;
 if(r->common3){for(unsigned i=1;i<=r->common3_count;i++)XbNativeTexture_Free(r->common3[i]);free(r->common3);}
 XbNativeTexture_Free(r->light);free(r->file);XbBattleCommon_Free(r->common);free(r);return 0;
}
int XbBattleRuntime_BeginManagers(XbBattleRuntime *r){
 if(!r || active!=r || !XbBattleRuntime_HasResources() || r->managers || gBtlChars || gBtlAi || gSaveData || gProgress || gStgNav || !gBtlObjWork)return -891;
 /* Validate every stage start before AllocAll performs its immediate reset. */
 for(int side=0;side<2;side++){
  BtlObj *obj=BtlObj_Get(side);float pos[4],rot[4];int zone=-1;
  if(!obj || !obj->active || obj->type!=BTL_OBJ_TYPE_CHARA || !obj->charaWork)return -891;
  if(!XbStage_TryStartPlace(side,pos,rot,0,&zone) || zone<0)return -892;
 }
 Progress_Init();Save_Init();Pad_Init();Battle_ClearWork();
 BattleSetup_SetRule(0,0,0,0,0,0,0);
 BattleItemSet items={{0}};
 for(int side=0;side<2;side++){
  BattleSetup_SetSide(side,side?BTL_CONTROL_CPU:BTL_CONTROL_PAD,side,1,0,0,0,NULL);
  BattleSetup_SetMember(side,0,side,0,0,13,100.0f,&items);
 }
 BattleSetup_Finish();
 for(int side=0;side<2;side++){BattleSide_SetObjId(side,side);BattleSide_SetModelSlot(side,side);}
 BtlChar_AllocAll(2);BtlAiMgr_Init();if(XbBattleStage_HasParameters())StgNav_Init();r->managers=1;return 0;
}
int XbBattleRuntime_EndManagers(XbBattleRuntime *r){
 if(!r || active!=r || !r->managers || !gBtlChars || !gBtlAi || !gSaveData || !gProgress)return -891;
 if(r->audio){int e=XbBattleRuntime_EndAudio(r);if(e)return e;}
 if(XbBattleStage_HasParameters())StgNav_Term();
 BtlAiMgr_Term();BtlChar_FreeAll();
 Heap_Free(gSaveData);gSaveData=NULL;
 Heap_Free(gProgress->loadSprites);Heap_Free(gProgress->loadRes);Heap_Free(gProgress->loadPack);Heap_Free(gProgress);gProgress=NULL;
 r->managers=0;return 0;
}

int XbBattleRuntime_GetInfo(XbBattleRuntime *r,XbBattleRuntimeInfo *out){
 if(!out)return -890;
 memset(out,0,sizeof(*out));
 if(!r || active!=r || !r->managers || !gBtlChars || gBtlChars->count!=2 || !gBtlAi)return -891;
 for(unsigned i=0;i<2;i++){
  out->health[i]=gBtlChars->chars[i].members[0].gauge.health;
  out->ki[i]=gBtlChars->chars[i].members[0].gauge.ki;
  out->ai_type[i]=gBtlAi->work[i].type;out->ai_level[i]=gBtlAi->work[i].level;
 }
 return 0;
}

#include "xb_battle_audio.inc"

#include "xb_battle_common3.inc"
#include "xb_character_effects.inc"

#include "xb_battle_files.inc"
