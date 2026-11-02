/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "port_compat.h"
#undef malloc
#include "xb_battle_runtime.h"
#include "xb_battle_fighter.h"
#include "xb_heap.h"
#include "battle/btl_obj.h"
#include "battle/btl_char_mgr.h"
#include "battle/btl_ai_think.h"
#include "battle/view_a.h"
#include "sys/common.h"
#include "sys/save.h"
#include "sys/heap.h"
#include "sys/gfxm_a.h"
#include "sys/gfxm_b.h"
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
BtlObjWork *gBtlObjWork;BtlObj *gBtlObjTbl[BTL_OBJ_MAX];
BtlCharMgr *gBtlChars;AiThMgr *gBtlAi;CommonRes *gCommonRes;SaveData *gSaveData;ViewProgress *gProgress;
ObjOutline *gObjOutline;ObjGlow *gObjGlow;GfxClutWork *gGfxAlphaKey;
HeapBlock *gHeapStart[2],*gHeapEnd[2];s32 gHeapSize[2],gHeapDebugValue,gHeapDebugUnused,gHeapUnk70[2],gHeapUnk78[2];u8 gHeapDebugFlag;
float D_002F2420[213],D_002EC2C0[4]={0,0,0,1},D_003337D0[4];
extern const float real_reference[213];
extern s32 Heap_GetFreeSize(void),Heap_GetUsedSize(void);
void *Bpe_Decode(void *s,void *d,int *n){(void)s;(void)d;(void)n;abort();}
void *XbBpe_DecodeBounded(void *s,void *d,unsigned n,int *z){(void)s;(void)d;(void)n;(void)z;abort();}
void xsdk_log_write(const char *fmt,...){va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);fputc('\n',stderr);va_end(ap);}
static void *load(const char *name,size_t *n){FILE *f=fopen(name,"rb");assert(f && !fseek(f,0,SEEK_END));long s=ftell(f);assert(s>0 && !fseek(f,0,SEEK_SET));*n=(size_t)s;void *p=malloc(*n);assert(p && fread(p,1,*n,f)==*n);fclose(f);return p;}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IOLBF,0);assert(argc==7);memcpy(D_002F2420,real_reference,sizeof(D_002F2420));
 size_t cn,in;void *common=load(argv[3],&cn),*items=load(argv[4],&in);XbBattleRuntime *runtime=NULL;
 assert(!XbBattleRuntime_Open(common,cn,items,in,&runtime));free(common);free(items);
 assert(XbBattleRuntime_HasResources() && XbHeap_Ready());s32 before=Heap_GetFreeSize();

 for(unsigned pass=0;pass<2;pass++){
  assert(!XbBattleFighter_BeginOriginalObjects());assert(XbBattleFighter_BeginOriginalObjects()<0);
  assert(gBtlObjWork && gObjOutline && gObjGlow && gGfxAlphaKey && gBtlObjWork->light.res);
  assert(Heap_GetFreeSize()<before);assert(XbBattleRuntime_Close(runtime)<0);
  XbBattleFighter *fighters[2]={0};
  for(unsigned side=0;side<2;side++){
   size_t mn,an;void *m=load(argv[side?5:1],&mn),*a=load(argv[side?6:2],&an);XbFighterPrepared prepared;
   assert(!XbBattleFighter_PrepareModelAnimation(m,mn,a,an,&fighters[side],&prepared));free(m);free(a);
   assert(!XbBattleFighter_BindCommonReference(fighters[side],real_reference,213));
   int id=-1;assert(!XbBattleFighter_CreateModelObject(fighters[side],side,&id) && id==(int)side);
   assert(BtlObj_Get(id)->active && BtlObj_Get(id)->charaWork);
   assert(!XbBattleFighter_PlayMotion(fighters[side],0,2));uint32_t frame,length;
   assert(!XbBattleFighter_StepModel(fighters[side],&frame,&length));assert(!XbBattleFighter_SamplePose(fighters[side]));assert(!XbBattleFighter_CalcMatrices(fighters[side]));
  }
  assert(BtlObj_GetCount()==2 && XbBattleFighter_EndObjectDiagnostic()<0);
  for(unsigned side=0;side<2;side++)XbBattleFighter_FreeModelAnimation(fighters[side]);
  assert(!XbBattleFighter_EndObjectDiagnostic());assert(!gBtlObjWork && !gObjOutline && !gObjGlow && !gGfxAlphaKey);
  assert(Heap_GetFreeSize()==before);
  printf("PASS original BtlObj_Init(1)/Term, real models and draw-module allocation, original heap restored: pass=%u free=%d\n",pass,before);
 }
 assert(!XbBattleRuntime_Close(runtime));assert(!gCommonRes && !XbBattleRuntime_HasResources());return 0;
}
