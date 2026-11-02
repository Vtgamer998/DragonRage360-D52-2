/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Native stage model: typed draw/collision structures; canonical VIF/GIF.
 * The original BtlStage_Init/Term own the physics/reset/ambience lifecycle. */
#include "xb_model_native.h"
#include "xb_adxt.h"
#include "battle/stg_d.h"
#include "battle/stgm_a.h"
#include "sys/common.h"
#include "sys/file.h"
#include "sys/heap.h"
extern s32 Heap_GetFreeSize(void);
typedef struct StageBuild {XbBattleStageCollision *owner;DrStageCollision input;uint8_t *dest,*seen;uint32_t visibility,texture_count;XbStageGeometryInfo info;} StageBuild;
static uint16_t geom_half(const uint8_t *p){return (uint16_t)(p[0]|(unsigned)p[1]<<8);}
static int geom_span(StageBuild *b,uint32_t word,size_t count,size_t stride,size_t *at){
 if(count>b->input.size/stride)return DR_STAGE_BOUNDS;
 return range(&b->input,word,count*stride,at);
}
static int geom_words(StageBuild *b,size_t at,size_t n){
 if(at>b->input.size || n>b->input.size-at || (n&3))return DR_STAGE_BOUNDS;
 words(b->dest+at,b->input.base+at,n);return 0;
}
static int geom_seen(StageBuild *b,size_t at,unsigned bit){
 unsigned char *p=b->seen+at/4;if(*p&bit)return 1;*p|=(uint8_t)bit;return 0;
}
static void geom_halves(StageBuild *b,size_t at,unsigned n){param_halves(b->dest+at,b->input.base+at,n);}
static int geom_node(StageBuild *b,uint32_t word){
 size_t at;if(geom_span(b,word,1,80,&at) || (at&15))return DR_STAGE_BOUNDS;
 if(geom_seen(b,at,4))return 0;
 const uint8_t *p=b->input.base+at;if(param_finite(p,16))return DR_STAGE_NUMBER;
 uint32_t count=rd(p+64);size_t keys;if(geom_span(b,rd(p+68),count,32,&keys))return DR_STAGE_BOUNDS;
 geom_words(b,at,80);
 uint32_t previous=0;
 for(uint32_t i=0;i<count;i++){
  const uint8_t *q=b->input.base+keys+(size_t)i*32;uint32_t frame=rd(q+12);
  if((i && frame<previous) || param_finite(q,3) || param_finite(q+16,3))return DR_STAGE_NUMBER;
  previous=frame;geom_words(b,keys+(size_t)i*32,32);
 }
 b->info.nodes++;return 0;
}
static int geom_mesh(StageBuild *b,uint32_t word){
 size_t at;if(geom_span(b,word,1,16,&at))return DR_STAGE_BOUNDS;
 if(geom_seen(b,at,1))return 0;
 const uint8_t *p=b->input.base+at;
 if(geom_half(p)>=b->visibility)return DR_STAGE_BOUNDS;
 size_t chain,mtx;if(geom_span(b,rd(p+8),1,16,&chain) || (chain&15) || geom_span(b,rd(p+12),1,64,&mtx))return DR_STAGE_BOUNDS;
 uint32_t tag=rd(b->input.base+chain);size_t bytes=((size_t)(tag&65535)+1)*16;
 /* Inline RET streams: keep every DMA/VIF/GIF word canonical. The GPU
  * interpreter still validates and executes the payload on submission. */
 if((tag>>28&7)!=6 || bytes>b->input.size-chain)return DR_STAGE_FORMAT;
 if(rd(p+12)){
  if(param_finite(b->input.base+mtx,16))return DR_STAGE_NUMBER;
  geom_words(b,mtx,64);
 }
 geom_halves(b,at,2);geom_words(b,at+8,8);b->info.meshes++;return 0;
}
static int geom_part(StageBuild *b,uint32_t word){
 size_t at;if(geom_span(b,word,1,32,&at))return DR_STAGE_BOUNDS;
 if(geom_seen(b,at,2))return 0;
 const uint8_t *p=b->input.base+at;uint32_t n=geom_half(p);size_t list;
 if(geom_span(b,rd(p+4),n,4,&list) || param_finite(p+12,4))return DR_STAGE_BOUNDS;
 geom_halves(b,at,4);geom_words(b,at+4,28);geom_words(b,list,(size_t)n*4);
 for(uint32_t i=0;i<n;i++){int e=geom_mesh(b,rd(b->input.base+list+i*4));if(e)return e;}
 if(rd(p+8)){int e=geom_node(b,rd(p+8));if(e)return e;}
 b->info.parts++;return 0;
}
static int geom_cell(StageBuild *b,uint32_t word,unsigned depth){
 size_t at;if(depth>32 || geom_span(b,word,1,64,&at))return DR_STAGE_TREE;
 if(b->seen[at/4]&8)return DR_STAGE_TREE;
 if(b->seen[at/4]&16)return 0;
 b->seen[at/4]|=8;
 const uint8_t *p=b->input.base+at;uint32_t count=rd(p+56);size_t list;
 if(rd(p+52)>30 || param_finite(p+40,3) || geom_span(b,rd(p+60),count,4,&list))return DR_STAGE_BOUNDS;
 geom_words(b,at,64);geom_words(b,list,(size_t)count*4);
 if(rd(p)){size_t ignored;if(geom_span(b,rd(p),1,4,&ignored))return DR_STAGE_BOUNDS;}
 for(uint32_t i=0;i<count;i++){uint32_t w=rd(b->input.base+list+i*4);if(w && w!=UINT32_MAX){int e=geom_part(b,w);if(e)return e;}}
 for(unsigned i=0;i<8;i++)if(rd(p+4+i*4)){int e=geom_cell(b,rd(p+4+i*4),depth+1);if(e)return e;}
 b->seen[at/4]=(uint8_t)((b->seen[at/4]&~8u)|16);b->info.cells++;return 0;
}
static int geom_tables(StageBuild *b){
 const uint8_t *h=b->input.base;size_t at;
 if(param_finite(h+76,1) || !(rf(h+76)>0))return DR_STAGE_NUMBER;
 b->visibility=rd(h+40);if(!b->visibility || b->visibility>65536 || geom_span(b,rd(h+44),b->visibility,1,&at))return DR_STAGE_BOUNDS;
 geom_words(b,0,96);uint32_t not_ready=1;memcpy(b->dest+8,&not_ready,4);
 /* Materials and meshes. */
 uint32_t groups=rd(h+24);if(groups!=7 || geom_span(b,rd(h+28),groups,8,&at))return DR_STAGE_FORMAT;
 b->info.groups=groups;
 for(unsigned g=0;g<groups;g++){
  size_t group=at+g*8;const uint8_t *p=h+group;uint32_t n=geom_half(p+2);size_t mats;
  if(geom_span(b,rd(p+4),n,16,&mats))return DR_STAGE_BOUNDS;
  geom_halves(b,group,4);geom_words(b,group+4,4);
  for(unsigned m=0;m<n;m++){
   size_t mat=mats+m*16;const uint8_t *q=h+mat;uint32_t count=geom_half(q+2);size_t meshes;
   if(geom_half(q)>=b->texture_count || geom_span(b,rd(q+4),count,16,&meshes))return DR_STAGE_BOUNDS;
   geom_halves(b,mat,4);geom_words(b,mat+4,8);geom_halves(b,mat+12,4);
   if(rd(q+8)){
    size_t anim,frames;if(geom_span(b,rd(q+8),1,8,&anim))return DR_STAGE_BOUNDS;
    uint32_t nf=geom_half(h+anim+2);if(!nf || geom_span(b,rd(h+anim+4),nf,4,&frames))return DR_STAGE_BOUNDS;
    geom_halves(b,anim,4);geom_words(b,anim+4,4);geom_words(b,frames,(size_t)nf*4);
    for(unsigned f=0;f<nf;f++)if((rd(h+frames+f*4)&65535)>=b->texture_count)return DR_STAGE_BOUNDS;
   }
   for(unsigned i=0;i<count;i++){int e=geom_mesh(b,(uint32_t)(meshes/4+i*4));if(e)return e;}
   b->info.materials++;
  }
 }
 /* Part lists and nodes. */
 uint32_t lists=rd(h+32);if(geom_span(b,rd(h+36),lists,8,&at))return DR_STAGE_BOUNDS;
 geom_words(b,at,(size_t)lists*8);
 for(unsigned i=0;i<lists;i++){
  const uint8_t *p=h+at+i*8;uint32_t count=rd(p);size_t parts;
  if(geom_span(b,rd(p+4),count,32,&parts))return DR_STAGE_BOUNDS;
  for(unsigned j=0;j<count;j++){int e=geom_part(b,(uint32_t)(parts/4+j*8));if(e)return e;}
 }
 uint32_t nodes=rd(h+48);if(geom_span(b,rd(h+52),nodes,80,&at))return DR_STAGE_BOUNDS;
 for(unsigned i=0;i<nodes;i++){int e=geom_node(b,(uint32_t)(at/4+i*20));if(e)return e;}
 int e=geom_cell(b,rd(h+80),0);if(e)return e;
 /* Collision zones use word offsets, including their numeric mesh data. */
 uint32_t zones=rd(h+56);if(geom_span(b,rd(h+60),zones,64,&at))return DR_STAGE_BOUNDS;
 XbBattleStageCollision converter={0};converter.source=b->input;converter.arena=b->dest;
 for(unsigned i=0;i<zones;i++){
  size_t zone=at+i*64;const uint8_t *p=h+zone;size_t near,recs;void *ignored;
  if(geom_span(b,rd(p+12),rd(p+8),4,&near) || geom_span(b,rd(p+24),rd(p+20),16,&recs))return DR_STAGE_BOUNDS;
  geom_words(b,zone,64);geom_words(b,near,(size_t)rd(p+8)*4);geom_words(b,recs,(size_t)rd(p+20)*16);
  e=native_mesh(&converter,rd(p+4),&ignored);if(e)return e;
  if(!rd(p+4)){uint32_t empty=(uint32_t)((b->owner->render_bytes-b->owner->render_offset-32)/4);memcpy(b->dest+zone+4,&empty,4);}
  for(unsigned j=0;j<rd(p+20);j++){
   uint32_t mesh=rd(h+recs+j*16+8);e=native_mesh(&converter,mesh,&ignored);if(e)return e;
   if(!mesh){uint32_t empty=(uint32_t)((b->owner->render_bytes-b->owner->render_offset-32)/4);memcpy(b->dest+recs+j*16+8,&empty,4);}
  }
 }
 if(geom_span(b,rd(h+64),1,12,&at))return DR_STAGE_BOUNDS;
 geom_words(b,at,12);
 /* Destructible objects and their debris lists. */
 uint32_t objects=rd(h+68);if(objects!=b->input.objects || geom_span(b,rd(h+72),objects,80,&at))return DR_STAGE_BOUNDS;
 b->info.objects=objects;geom_words(b,at,(size_t)objects*80);
 for(unsigned i=0;i<objects;i++){
  const uint8_t *p=h+at+i*80;uint32_t count=rd(p+36);size_t pieces;
  if(geom_span(b,rd(p+40),count,4,&pieces))return DR_STAGE_BOUNDS;
  geom_words(b,pieces,(size_t)count*4);
  for(unsigned k=0;k<6;k++)if(rd(p+12+k*4)){e=geom_part(b,rd(p+12+k*4));if(e)return e;}
  for(unsigned k=0;k<count;k++){e=geom_part(b,rd(h+pieces+k*4));if(e)return e;}
 }
 uint32_t timers=rd(h+16);if(geom_span(b,rd(h+20),timers,16,&at))return DR_STAGE_BOUNDS;
 for(unsigned i=0;i<timers;i++){geom_halves(b,at+i*16,4);geom_words(b,at+i*16+4,12);if(param_finite(h+at+i*16+8,2))return DR_STAGE_NUMBER;}
 b->info.timers=timers;return 0;
}
#include "xb_stage_animations.inc"
int XbBattleStage_FreeGeometry(XbBattleStageCollision *c){
 if(!c || c->render_live)return XB_STAGE_STATE;
 for(unsigned i=0;i<c->animation_texture_count;i++)XbNativeTexture_Free(c->animation_textures[i]);
 free(c->animation_textures);c->animation_textures=NULL;c->animation_texture_count=c->animation_models=c->animation_nodes=0;
 free(c->render_pack);XbNativeTexture_Free(c->render_texture);c->render_pack=NULL;c->render_texture=NULL;c->render_bytes=0;c->render_bound=0;c->render_error=0;memset(&c->geometry,0,sizeof(c->geometry));return 0;
}
int XbBattleStage_PrepareGeometry(XbBattleStageCollision *c,const void *pack,size_t n,XbStageGeometryInfo *info){
 if(info)memset(info,0,sizeof(*info));
 if(!c || !pack || sizeof(void *)!=4 || !c->parameters || c->render_pack || c->render_live)return XB_STAGE_STATE;
 DrStageCollision v;int e=dr_stage_collision_open(pack,n,&v);if(e)return e;
 size_t at,bytes;unsigned flags;if(dr_pack_memory_entry(pack,n,0,&at,&bytes,&flags) || flags || (n&15))return DR_STAGE_FORMAT;

 c->render_pack=malloc(n+32);if(!c->render_pack)return XB_STAGE_MEMORY;
 c->render_bytes=n+32;c->render_offset=at;memcpy(c->render_pack,pack,n);memset(c->render_pack+n,0,32);
 words(c->render_pack,pack,(rd(pack)+2)*4);
 StageBuild b={.owner=c,.input=v,.dest=c->render_pack+at};b.seen=calloc(1,v.size/4+1);
 if(!b.seen){e=XB_STAGE_MEMORY;goto fail;}
 size_t tex;if(geom_span(&b,rd(v.base+84),1,32,&tex)){e=DR_STAGE_BOUNDS;goto cleanup;}
 uint8_t *texture=malloc(v.size);if(!texture){e=XB_STAGE_MEMORY;goto cleanup;}
 memcpy(texture,v.base,v.size);memcpy(texture,v.base+tex,32);
 e=XbNativeTexture_Prepare(texture,v.size,&c->render_texture);free(texture);if(e)goto cleanup;
 b.texture_count=rd(v.base+tex);b.info.textures=b.texture_count;
 /* Stage entries 2..4 are the three original intro cameras. */
 for(unsigned camera=2;camera<=4;camera++){
  size_t ca,cn;unsigned cf;int ce=dr_pack_memory_entry(pack,n,camera,&ca,&cn,&cf);
  if(ce==DR_NOT_FOUND)continue;
  if(ce || cf){e=DR_STAGE_FORMAT;goto cleanup;}
  e=XbStageCamera_Prepare(c->render_pack+ca,(const uint8_t *)pack+ca,cn);
  if(e)goto cleanup;
 }
 /* Texture packs consumed by scroll, stage sprites and boundary tasks. */
 static const unsigned textures[]={5,19,20},limits[]={32,4,32};
 for(unsigned i=0;i<3;i++){
  size_t ta,tn;unsigned tf;int te=dr_pack_memory_entry(pack,n,textures[i],&ta,&tn,&tf);
  if(te==DR_NOT_FOUND)continue;
  if(te || tf){e=DR_STAGE_FORMAT;goto cleanup;}
  void *native=NULL;e=XbNativeTexture_Prepare((const uint8_t *)pack+ta,tn,&native);if(e)goto cleanup;
  if(rd((const uint8_t *)pack+ta)>limits[i]){XbNativeTexture_Free(native);e=DR_STAGE_BOUNDS;goto cleanup;}
  memcpy(c->render_pack+ta,native,tn);XbNativeTexture_Free(native);
  /* The original consumer will rebase all pointers from the native offsets. */
 }
 static const unsigned effects[]={6,12,21,22};
 for(unsigned i=0;i<4;i++){
  size_t ea,en;unsigned ef;int ee=dr_pack_memory_entry(pack,n,effects[i],&ea,&en,&ef);
  if(ee==DR_NOT_FOUND)continue;
  if(ee || ef){e=DR_STAGE_FORMAT;goto cleanup;}
  e=XbStageEffects_PrepareEntry(effects[i],c->render_pack+ea,(const uint8_t *)pack+ea,en);if(e)goto cleanup;
  if(effects[i]==12){const uint8_t *raw=(const uint8_t *)pack+ea+64;unsigned count=rd(raw+24),offset=rd(raw+28);c->surface_meshes=count;c->surface_triangles=0;
   for(unsigned m=0;m<count;m++)c->surface_triangles+=rd(raw+offset+(size_t)m*96+4);
  }
 }
 e=geom_tables(&b);if(!e)e=geom_animations(c,pack,n);if(!e){c->geometry=b.info;if(info)*info=b.info;}
cleanup:free(b.seen);
fail:if(e)XbBattleStage_FreeGeometry(c);return e;
}
int XbStage_TryBindGeometryData(void *stage){
 if(!active || !active->render_live || stage!=active->render_pack+active->render_offset)return 0;
 ((BtlStage *)stage)->data=(StgDataA *)active->parameters;
 if(!active->render_bound){
  StgMModel *model=((BtlStageM *)stage)->mdl;
  for(unsigned g=0;g<7;g++)for(unsigned i=0;i<model->grp[g].count;i++){
   StgMTexAnim *anim=model->grp[g].mats[i].anim;
   if(anim && (uintptr_t)anim->frames<active->render_bytes/4)
    anim->frames=(s32 *)((uint8_t *)stage+(uintptr_t)anim->frames*4);
  }
  active->render_bound=1;
 }
 return 1;
}
int XbStage_TryBindGeometryTexture(void *base,void **out){
 if(!out || !active || !active->render_live || base!=active->render_pack+active->render_offset)return 0;
 *out=active->render_texture;return 1;
}
int XbBattleStage_BeginGeometry(XbBattleStageCollision *c){
 if(!c || active!=c || !c->installed || c->render_live || !c->render_pack || !gCommonRes || (void *)gBtlStage!=&c->stage || gStgRigid)return XB_STAGE_STATE;
 extern AdxPlayer gAdxPlayerTbl[];
 for(unsigned i=0;i<ADX_PLAYER_COUNT;i++)if(!gAdxPlayerTbl[i].adxt)return XB_STAGE_STATE;
 if(Heap_GetFreeSize()<(s32)sizeof(StgRigidPool)+4096)return XB_STAGE_MEMORY;
 void **slot=(void **)((uint8_t *)gCommonRes+0x24);c->saved_pack=*slot;*slot=c->render_pack;c->render_live=1;
 extern void StgModel_InitStage(void);StgModel_InitStage();
 if((void *)gBtlStage!=c->render_pack+c->render_offset || !BtlStage_IsReady() || !gStgRigid || c->render_error || XbAdx_LastError()){
  XbBattleStage_EndGeometry(c);return XB_STAGE_STATE;
 }
 return 0;
}
int XbBattleStage_EndGeometry(XbBattleStageCollision *c){
 if(!c || active!=c || !c->render_live || (void *)gBtlStage!=c->render_pack+c->render_offset)return XB_STAGE_STATE;
 BtlStage_Term();gBtlStage=(BtlStage *)(void *)&c->stage;gColMeshBase=(s32 *)c->arena;
 void **slot=(void **)((uint8_t *)gCommonRes+0x24);*slot=c->saved_pack;c->render_live=0;
 return XbBattleStage_FreeGeometry(c);
}

/* Diagnostic original-consumer check after BtlCam_Init, before scene frames. */
#include "battle/btl_demo_cam.h"
int XbBattleStage_CheckCameras(XbBattleStageCollision *c,unsigned *endpoints){
 if(endpoints)*endpoints=0;
 if(!c || active!=c || !c->render_live || !gDemoCam || !endpoints)return XB_STAGE_STATE;
 unsigned count=0;int error=0;
 for(unsigned index=2;index<=4 && !error;index++){
  uint32_t a,b;memcpy(&a,c->render_pack+4+index*4,4);memcpy(&b,c->render_pack+8+index*4,4);
  if(a==b)continue;
  DemoCamAnim *anim=(DemoCamAnim *)(c->render_pack+a);DemoCam_SetAnim(anim);
  if(anim->needsFixup){error=DR_STAGE_FORMAT;break;}
  for(int i=0;i<anim->channelCount && !error;i++){
   DemoCamChannel *channel=&anim->channels[i];
   for(unsigned last=0;last<2;last++){
    DemoCamKey *key=&channel->keys[last?channel->keyCount-1:0];DemoCamPose pose={0};
    DemoCam_EvalAnim(&pose,key->time);
    if(pose.v[channel->index]!=key->value){error=DR_STAGE_NUMBER;break;}
    count++;
   }
  }
 }
 DemoCam_Reset();*endpoints=count;return error;
}
#include "battle/eft_ad.h"
#include "battle/eft_ae.h"
extern void EftTexSet_Load32(EftAeTexSet32 *,void *);
extern void EftTexSet_Load4(EftAeTexSet4 *,void *);
int XbBattleStage_CheckEffects(XbBattleStageCollision *c,unsigned *textures,unsigned *groups){
 if(textures)*textures=0;
 if(groups)*groups=0;
 if(!c || active!=c || !c->render_live || !textures || !groups)return XB_STAGE_STATE;
 static const unsigned ids[]={5,19,20};unsigned scroll_count=0;
 for(unsigned i=0;i<3;i++){
  uint32_t a,b;memcpy(&a,c->render_pack+4+ids[i]*4,4);memcpy(&b,c->render_pack+8+ids[i]*4,4);if(a==b)continue;
  if(ids[i]==19){EftAeTexSet4 set;EftTexSet_Load4(&set,c->render_pack+a);if(set.count<1 || set.count>4)return DR_STAGE_FORMAT;*textures+=(unsigned)set.count;}
  else{EftAeTexSet32 set;EftTexSet_Load32(&set,c->render_pack+a);if(set.count<1 || set.count>32)return DR_STAGE_FORMAT;*textures+=(unsigned)set.count;if(ids[i]==5)scroll_count=(unsigned)set.count;}
 }
 uint32_t a,b;memcpy(&a,c->render_pack+28,4);memcpy(&b,c->render_pack+32,4);
 if(a!=b){EftMesh mesh;EftMesh_Init(&mesh,(EftMeshData *)(c->render_pack+a+16));
  if(mesh.data->numGroups<1 || mesh.groups!=mesh.data->groups || mesh.uvScale!=1.0f)return DR_STAGE_FORMAT;
  for(int g=0;g<mesh.data->numGroups;g++)for(int t=0;t<mesh.groups[g].numTris;t++){
   int texture=mesh.tris[mesh.groups[g].firstTri+t].tex;if(texture>=0 && (unsigned)texture>=scroll_count)return DR_STAGE_BOUNDS;
  }
  *groups=(unsigned)mesh.data->numGroups;
 }
 return 0;
}
#include "battle/eft_c.h"
extern void EftSurf_Relocate(EftSurfFile *);
int XbBattleStage_CheckSurfaces(XbBattleStageCollision *c,unsigned *meshes,unsigned *triangles){
 if(meshes)*meshes=0;
 if(triangles)*triangles=0;
 if(!c || active!=c || !c->render_live || !meshes || !triangles)return XB_STAGE_STATE;
 uint32_t a,b;memcpy(&a,c->render_pack+52,4);memcpy(&b,c->render_pack+56,4);
 if(a==b)return 0;
 EftSurfFile *file=(EftSurfFile *)(c->render_pack+a+64);size_t bytes=b-a-64;
 if(!gEftSurf || gEftSurf->file!=file || file->version!=4 || file->texCount<0 || file->meshCount<0)return XB_STAGE_STATE;
 uintptr_t base=(uintptr_t)file,tx=(uintptr_t)file->texs,ms=(uintptr_t)file->meshes;
 if(tx<base || tx-base>bytes || (size_t)file->texCount>(bytes-(tx-base))/64 || ms<base || ms-base>bytes || (size_t)file->meshCount>(bytes-(ms-base))/96)return DR_STAGE_BOUNDS;
 for(int i=0;i<file->texCount;i++){
  EftSurfTex *t=&file->texs[i];
  if((uint32_t)t->ptr0!=(uint32_t)base+(uint32_t)t->ofs0 || (uint32_t)t->ptr4!=(uint32_t)base+(uint32_t)t->ofs4)return DR_STAGE_FORMAT;
 }
 for(int i=0;i<file->meshCount;i++){
  EftSurfMesh *m=&file->meshes[i];uintptr_t ptr=(uintptr_t)m->tris;
  if(m->triCount<0 || ptr<base || ptr-base>bytes || (size_t)m->triCount>(bytes-(ptr-base))/144)return DR_STAGE_BOUNDS;
  *triangles+=(unsigned)m->triCount;(*meshes)++;
 }
 return *meshes==c->surface_meshes && *triangles==c->surface_triangles?0:DR_STAGE_FORMAT;
}
#include "xb_disc.h"
#include <stdio.h>
int XbBattleStage_CheckAllCameras(unsigned *cameras,unsigned *endpoints){
 if(cameras)*cameras=0;
 if(endpoints)*endpoints=0;
 if(!gDemoCam || !cameras || !endpoints || !XbDisc_Get())return XB_STAGE_STATE;
 int error=0;
 for(unsigned stage=0;stage<39 && !error;stage++){
  char name[48];snprintf(name,sizeof(name),"pzs3us1/%05u.bin",0x170+stage);dr_span file;
  error=XbDisc_FindFile(name,&file);if(error)break;
  if(!file.size || file.size>8*1024*1024){error=DR_STAGE_BOUNDS;break;}
  uint8_t *pack=malloc((size_t)file.size);if(!pack){error=XB_STAGE_MEMORY;break;}
  error=XbDisc_ReadSpan(&file,0,pack,(size_t)file.size);
  for(unsigned index=2;index<=4 && !error;index++){
   size_t at,n;unsigned flags;int e=dr_pack_memory_entry(pack,(size_t)file.size,index,&at,&n,&flags);
   if(e==DR_NOT_FOUND)continue;
   if(e || flags){error=DR_STAGE_FORMAT;break;}
   uint8_t *native=malloc(n);if(!native){error=XB_STAGE_MEMORY;break;}
   error=XbStageCamera_Prepare(native,pack+at,n);
   if(!error){DemoCamAnim *anim=(DemoCamAnim *)native;DemoCam_SetAnim(anim);
    for(int i=0;i<anim->channelCount && !error;i++)for(unsigned end=0;end<2;end++){
     DemoCamChannel *ch=&anim->channels[i];DemoCamKey *key=&ch->keys[end?ch->keyCount-1:0];DemoCamPose pose={0};DemoCam_EvalAnim(&pose,key->time);
     if(pose.v[ch->index]!=key->value){error=DR_STAGE_NUMBER;break;}
     (*endpoints)++;
    }
    (*cameras)++;
   }
   DemoCam_Reset();free(native);
  }
  free(pack);
 }
 DemoCam_Reset();return error;
}
