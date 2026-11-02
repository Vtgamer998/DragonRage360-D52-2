/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "port_compat.h"
#undef malloc
#include "xb_battle_boot.h"
#include "xb_resconv.h"
#include "battle/btl_obj.h"
#include "sys/gfxm_e.h"
#include "sys/file.h"
#include "sys/job.h"
#include "sys/common.h"
#include "sys/loading.h"
#include "sys/save.h"
#include "sys/snd.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
/* Original object registry/pools in an isolated native model session.
 * CreateChara/Setup can activate models here; this is not a BtlChar/BtlAi
 * or battle boot. The session owns its aligned work and shadow storage. */
static BtlObjWork *diagnostic_work;
static void *diagnostic_allocation;
static int original_session;
static void (*original_term)(void);
static BtlResSlot original_slots[2];
static unsigned slot_used;
static BtlObj *saved_map[BTL_OBJ_MAX];
static void *shadow_allocations[OBJ_SHADOW_MAX][3];
static void *aligned_zero(size_t bytes,size_t align,void **raw){
 *raw=calloc(1,bytes+align-1);if(!*raw)return NULL;return (void *)(((uintptr_t)*raw+align-1)&~(uintptr_t)(align-1));
}
static void free_shadows(void){for(unsigned i=0;i<OBJ_SHADOW_MAX;i++)for(unsigned j=0;j<3;j++){free(shadow_allocations[i][j]);shadow_allocations[i][j]=NULL;}}
static int init_shadows(void){
 ObjShadowPool *pool=(ObjShadowPool *)&diagnostic_work->pool3;
 size_t bytes=(size_t)ObjShadow_CalcPacketWords(OBJ_SHADOW_PKT_TRIS)*4;
 for(unsigned i=0;i<OBJ_SHADOW_MAX;i++){
  ObjShadowWork *w=aligned_zero(sizeof(*w),32,&shadow_allocations[i][0]);if(!w)return -851;
  pool->nodes[i].work=w;w->pkt[0]=aligned_zero(bytes,64,&shadow_allocations[i][1]);w->pkt[1]=aligned_zero(bytes,64,&shadow_allocations[i][2]);
  if(!w->pkt[0] || !w->pkt[1])return -851;
  SList_PushFront(&pool->free,&pool->nodes[i].link);
 }
 return 0;
}
extern void BtlObj_Init(s32);
extern void BtlObj_Term(void);
extern int XbBattleRuntime_HasResources(void);
extern FileReqQueue gFileReq;
extern s32 gGscActive;
static int original_roots_present(void){
 if(!XbRes_HasCheckedConverter(2) || !XbRes_HasCheckedConverter(3) ||
    !XbRes_HasCheckedConverter(4) || !XbRes_HasCheckedConverter(5))return 0;
 if(!gCommonRes || !gCommonRes->boot || !gSaveData || !gProgress || !gSndMgr || !gSndRpc)return 0;
 if(!gProgress->loadPack || !gProgress->loadRes || !gProgress->loadSprites)return 0;
 for(unsigned i=0;i<COMMON_FILE_COUNT;i++)if(!gCommonRes->data[i] || gCommonRes->size[i]<=0)return 0;
 return 1;
}
int XbBattleBoot_ObjectStateClear(void){
 return XbRes_CheckedReadsActive() && original_roots_present()
     && !diagnostic_work && !gBtlObjWork && !XbBattleRuntime_HasResources()
     && !gGscActive && gJobQueue && gJobQueue->nodes && gJobQueue->capacity>=JOB_MAX
     && Job_GetCount()==0 && !File_IsBusy() && !gFileReq.adxf
     && File_GetPendingCount()==0 && List_GetCount(&gFileReq.free)==FILE_REQ_MAX;
}
int XbBattleBoot_OriginalObjectStateActive(void){
 return XbRes_CheckedReadsActive() && !diagnostic_work && gBtlObjWork && !XbBattleRuntime_HasResources()
     && gGscActive && gJobQueue && gJobQueue->nodes && gJobQueue->capacity>=JOB_MAX
     && List_GetCount(&gFileReq.free)+File_GetPendingCount()==FILE_REQ_MAX;
}
extern int XbHeap_Ready(void);
int XbFighterRegistry_BeginOriginal(void){
 if(sizeof(void *)!=4 || diagnostic_work || gBtlObjWork || !XbBattleRuntime_HasResources() || !XbHeap_Ready())return -865;
 memcpy(saved_map,gBtlObjTbl,sizeof(saved_map));
 BtlObj_Init(1); /* original heap, fixed resource buffers, light and draw modules */
 diagnostic_work=gBtlObjWork;original_session=1;original_term=BtlObj_Term;slot_used=0;
 return diagnostic_work?0:-851;
}
int XbFighterRegistry_Begin(void){
 if(sizeof(void *)!=4 || diagnostic_work || gBtlObjWork)return -865;
 diagnostic_work=aligned_zero(sizeof(*diagnostic_work),32,&diagnostic_allocation);if(!diagnostic_work)return -851;
 memcpy(saved_map,gBtlObjTbl,sizeof(saved_map));gBtlObjWork=diagnostic_work;
 BtlObj_InitTable();BtlObjPoolE0_Init();BtlObjPool70_Init();
 int e=init_shadows();if(e){free_shadows();gBtlObjWork=NULL;free(diagnostic_allocation);diagnostic_allocation=NULL;diagnostic_work=NULL;memcpy(gBtlObjTbl,saved_map,sizeof(saved_map));return e;}
 BtlObj_RebuildTable();return 0;
}
int XbFighterRegistry_Add(const void *obj,size_t bytes,void **live,int *id){
 if(!obj || !live || !id || bytes!=sizeof(BtlObj) || !diagnostic_work || gBtlObjWork!=diagnostic_work)return -865;
 int n=BtlObj_AllocId();if(n<0)return -866;
 BtlObj *p=BtlObj_Get(n);memcpy(p,obj,bytes);p->id=n;p->active=0;
 *live=p;*id=n;return 0;
}
int XbFighterRegistry_Remove(int id,void *copy,size_t bytes){
 if(!diagnostic_work || gBtlObjWork!=diagnostic_work || id<0 || id>=BTL_OBJ_MAX || bytes!=sizeof(BtlObj) || !copy)return -865;
 BtlObj *p=BtlObj_Get(id);if(!p)return -865;
 memcpy(copy,p,bytes);if(!BtlObj_FreeId(id))return -865;
 memset(p,0,bytes);BtlObj_RebuildTable();return 0;
}
int XbFighterRegistry_End(void){
 if(!diagnostic_work || gBtlObjWork!=diagnostic_work || BtlObj_GetCount()!=0)return -865;
 if(BtlObjPoolE0_GetFreeCount()!=512 || BtlObjPool70_GetFreeCount()!=4 || BtlObjPool3_GetFreeCount()!=OBJ_SHADOW_MAX)return -865;
 if(original_session){
  if(slot_used)return -865;
  original_term();original_term=NULL;original_session=0;
 }else{free_shadows();gBtlObjWork=NULL;free(diagnostic_allocation);diagnostic_allocation=NULL;}
 diagnostic_work=NULL;memcpy(gBtlObjTbl,saved_map,sizeof(saved_map));return 0;
}

extern void BtlObjMdl_Destroy(ObjMdlObj *);
int XbFighterRegistry_ModelCapacity(unsigned parts){
 if(!diagnostic_work || gBtlObjWork!=diagnostic_work || parts>512)return -865;
 if((unsigned)BtlObjPoolE0_GetFreeCount()<parts || BtlObjPool70_GetFreeCount()<1 || BtlObjPool3_GetFreeCount()<1 || (gBtlObjWork->vis.allocBits&0x7fff)==0x7fff)return -866;
 return 0;
}
int XbFighterRegistry_ModelSlot(int side,const void *model,size_t model_size,const void *motions,size_t motions_size,void **slot){
 if(!diagnostic_work || gBtlObjWork!=diagnostic_work || side<0 || side>=2 || !slot || !model || !motions || model_size>0x7fffffff || motions_size>0x7fffffff)return -865;
 BtlResSlot *s=&diagnostic_work->res.fixed[side];if(s->flags)return -865;
 if(original_session){
  if(model_size>BTL_RES_MODEL_SIZE || motions_size>BTL_RES_ANIM0_SIZE || !s->file[0].buf || !s->file[1].buf)return -866;
  original_slots[side]=*s;slot_used|=1u<<side;
  memcpy(s->file[0].buf,model,model_size);s->file[0].size=(s32)model_size;s->file[0].id=0x590+side*10;
  memcpy(s->file[1].buf,motions,motions_size);s->file[1].size=(s32)motions_size;s->file[1].id=0x598+side*10;
  s->flags=BTL_RES_SLOT_USED;*slot=s;return 0;
 }

 s->file[0].buf=(void *)model;s->file[0].size=(s32)model_size;s->file[0].id=0x590+side*10;
 s->file[1].buf=(void *)motions;s->file[1].size=(s32)motions_size;s->file[1].id=0x598+side*10;s->file[2].id=-1;
 s->number=side;s->flags=BTL_RES_SLOT_USED;diagnostic_work->res.prealloc=1;*slot=s;return 0;
}
void XbFighterRegistry_ReleaseModelSlot(int side){
 if(!diagnostic_work || side<0 || side>=2)return;
 if(original_session){
  if(slot_used&(1u<<side)){diagnostic_work->res.fixed[side]=original_slots[side];slot_used&=~(1u<<side);}
 }else memset(&diagnostic_work->res.fixed[side],0,sizeof(BtlResSlot));
}
int XbFighterRegistry_Setup(int id,void *slot){
 if(!diagnostic_work || gBtlObjWork!=diagnostic_work || id<0 || id>=2 || !BtlObj_Get(id) || !slot)return -865;
 BtlObj *obj=BtlObj_Get(id);BtlObj_Setup(obj,BTL_OBJ_TYPE_CHARA,slot,id,1);obj->charaWork=BtlObj_GetCharaWork(id);return 0;
}
void XbFighterRegistry_DestroyModel(int id){if(diagnostic_work && id>=0 && id<2 && BtlObj_Get(id))BtlObjMdl_Destroy((ObjMdlObj *)BtlObj_Get(id));}
int XbFighterRegistry_NextModel(void **object,int *id){
 if(!diagnostic_work || gBtlObjWork!=diagnostic_work || !object || !id)return -865;
 BtlObjNode *node=(BtlObjNode *)List_GetHead(&diagnostic_work->table.freeObjs);
 if(!node || node->id<0 || node->id>=2)return -866;
 *object=&node->obj;*id=node->id;return 0;
}
int XbFighterRegistry_CreateModel(int side){
 if(!diagnostic_work || gBtlObjWork!=diagnostic_work || side<0 || side>=2 || !(diagnostic_work->res.fixed[side].flags&BTL_RES_SLOT_USED))return -865;
 return BtlObj_CreateChara(side);
}
