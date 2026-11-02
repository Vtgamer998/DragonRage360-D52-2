/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "port_compat.h"
#include "xb_battle_runtime.h"
#include "battle/eft_h.h"
#include "battle/eft_ae.h"
#include "battle/eft_u.h"
#include "battle/eft_ad.h"
#include "battle/eft_ab.h"
#include "battle/btl_demo_cam.h"
#include "battle/btl_pool.h"
#include "sys/heap.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
extern void EftEmit_LoadSet(void *,EftSet *,EftSetHead *,s32 *,s32,s32);
extern void EftEmit_FreeSet(EftSet *),EftEmit_BeginFrame(EftSet *);
extern void EftSprPack_Bind(EftSprPack *);
extern s32 EftSprPack_FindKey(EftSprLayerDef *,s32);
extern s32 *BtlScene_GetCommonEntry(s32);
extern s32 Heap_GetFreeSize(void);
extern void EftSprAnim_InitPool(void),EftSprAnim_TermPool(void);
extern void EftSprAnim_Create(EftSprAnim *,EftSprPack *,EftAeTexSet32 *),EftSprAnim_Destroy(EftSprAnim *);
extern s32 EftSprAnim_Step(EftSprAnim *);
void verify_common3_consumers(void){
 static const unsigned sets[]={0xc,0x20,0x25,0x4c,0x66,0x73,0x88,0x91,0xa4,0xb9,0xf2,0x11a,0x137,0x158,0x192,0x1aa,0x1c0,0x1c1,0x1cb,0x1e6,0x1fc,0x217,0x23f,0x268};
 s32 heap=Heap_GetFreeSize();BtlPool_Init(0);BtlPool_SetCurrent(0);
 EftSprAnim_InitPool();unsigned sprites=0,layers=0,param_keys=0,animation_steps=0;
 for(unsigned i=0;i<24;i++){
  EftSet set={0};EftEmit_LoadSet(NULL,&set,NULL,NULL,1,(s32)sets[i]);assert(set.head && set.head->nGroups<=19);
  for(unsigned j=0;j<set.head->nGroups;j++){
   EftSetGroupDef *def=&set.entries[j];EftSetGroup *group=&set.group[def->kind];
   assert(group->parts==set.parts+group->firstPart && group->entry==def);
   if(!def->kind)continue;
   for(unsigned k=0;k<def->nRes;k++)assert(set.res[group->firstRes+k]);
   for(unsigned k=0;k<def->nParts;k++){
    EftSetPair *pair=&set.pair[group->firstPair+k];assert(pair->a);
    if(def->kind!=2 && def->kind!=14)assert(pair->b);
    if(def->kind==14){
     EftSprAnim anim;EftAeTexSet32 *texture=(EftAeTexSet32 *)(set.array[0]+(group->catFirst+group->parts[k].res)*0x208);
     EftSprAnim_Create(&anim,(EftSprPack *)pair->a,texture);
     for(unsigned tick=0;tick<40;tick++){
      assert(EftSprAnim_Step(&anim)>0);
      for(unsigned l=0;l<anim.pack->layerCount;l++)assert(isfinite(anim.slot->layer[l].frame) && isfinite(anim.slot->layer[l].alpha));
      animation_steps++;
     }
     EftSprAnim_Destroy(&anim);
    }
    if(def->kind==5){
     EftUPtclWork work={0};EftUPtclCur cur;
     work.arg.def=(EftUPtclDef *)pair->a;work.arg.def2=(EftUPtclDef2 *)pair->b;
     for(int key=0;key<3;key++){
      memset(&cur,0,sizeof(cur));EftPtcl_SetKey(&cur,&work,key);
      volatile float expected_life=work.arg.def->life[key]*30.0f;
      assert(cur.count==work.arg.def->count[key] && cur.life==expected_life);
      assert(isfinite(cur.life) && isfinite(cur.speed[0]));param_keys++;
     }
    }
   }
  }
  EftEmit_BeginFrame(&set);EftEmit_FreeSet(&set);assert(!set.head);
 }
 for(int id=1;id<=637;id++)if(XbBattleRuntime_Common3Type(id)==4){
  EftSprPack *sprite=(EftSprPack *)BtlScene_GetCommonEntry(id);assert(sprite && sprite->layerCount<=16);
  EftSprPack_Bind(sprite);sprites++;
  for(unsigned l=0;l<sprite->layerCount;l++){
   EftSprLayerDef *def=&sprite->layer[l];assert(def->keys && def->keyCount);layers++;
   for(unsigned k=0;k+1<def->keyCount;k++)assert(EftSprPack_FindKey(def,def->keys[k].frame)>=0);
  }
 }
 for(unsigned i=0;i<2;i++){
  EftMesh mesh;EftMeshData *data=(EftMeshData *)BtlScene_GetCommonEntry(i?0x23b:0x239);EftMesh_Init(&mesh,data);
  assert(mesh.data==data && mesh.groups==data->groups && mesh.tris && mesh.verts && mesh.mtx.m[3][3]==1);
 }
 EftSprAnim_TermPool();BtlPool_Term();assert(Heap_GetFreeSize()==heap);
 printf("PASS original common3 consumers: 24 sets, %u sprite packs, %u layers, %u particle keys, %u sprite steps, 2 meshes; heap restored\n",sprites,layers,param_keys,animation_steps);
}
void verify_character_effect_consumers(void *root){
 extern s32 *BtlScene_GetPackEntry(s32 *,s32);
 s32 heap=Heap_GetFreeSize();BtlPool_Init(0);BtlPool_SetCurrent(0);
 static const unsigned ids[]={2,3,4,7};
 for(unsigned i=0;i<4;i++){
  EftSet set={0};s32 *pack=BtlScene_GetPackEntry(root,(s32)ids[i]);
  EftEmit_LoadSet(NULL,&set,NULL,pack,2,1);assert(set.head);
  for(unsigned j=0;j<set.head->nGroups;j++){
   EftSetGroupDef *group=&set.entries[j];if(!group->kind)continue;
   EftSetGroup *live=&set.group[group->kind];
   for(unsigned k=0;k<group->nRes;k++)assert(set.res[live->firstRes+k]);
   for(unsigned k=0;k<group->nParts;k++){
    EftSetPair *pair=&set.pair[live->firstPair+k];assert(pair->a);
    if(group->kind==17){
     EftRbnArg arg={0};EftRbnCur cur={0};arg.prm=(EftRbnPrm *)pair->a;arg.anim=(EftRbnAnim *)pair->b;assert(arg.anim);
     for(int key=0;key<3;key++){
      EftRibbon_LoadKey(&cur,&arg,key);
      assert(cur.width==arg.prm->width[key] && isfinite(cur.pulseTime));
     }
    }
   }
  }
  EftEmit_BeginFrame(&set);EftEmit_FreeSet(&set);assert(!set.head);
 }
 BtlPool_Term();assert(Heap_GetFreeSize()==heap);
 puts("PASS four original character effect sets: Load/BeginFrame/Free; heap restored");
 heap=Heap_GetFreeSize();DemoCam_Init();unsigned endpoints=0;
 s32 *cameras=BtlScene_GetPackEntry(root,8);
 for(unsigned id=1;id<=4;id++){
  if(cameras[id]==cameras[id+1])continue;
  DemoCamAnim *anim=(DemoCamAnim *)BtlScene_GetPackEntry(cameras,(s32)id);if(!anim)continue;
  DemoCam_SetAnim(anim);assert(anim->needsFixup==0);
  for(int c=0;c<anim->channelCount;c++){
   DemoCamChannel *channel=&anim->channels[c];
   for(int end=0;end<2;end++){
    DemoCamKey *key=&channel->keys[end?channel->keyCount-1:0];DemoCamPose pose={0};
    DemoCam_EvalAnim(&pose,key->time);assert(pose.v[channel->index]==key->value);endpoints++;
   }
  }
 }
 DemoCam_Term();assert(Heap_GetFreeSize()==heap);
 printf("PASS original character camera fixup/evaluation: %u exact channel endpoints; heap restored\n",endpoints);
}
