/* SPDX-License-Identifier: AGPL-3.0-or-later */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "port_compat.h"
#undef malloc
#include "xb_battle_runtime.h"
#include "xb_battle_fighter.h"
#include "xb_battle_stage.h"
#include "xb_typed_data.h"
#include "battle/btl_char_mgr.h"
#include "battle/btl_ai_think.h"
#include "battle/btl_obj.h"
#include "battle/btl_input.h"
#include "battle/eft_det_b.h"
#include "battle/eft_g.h"
#include "battle/eft_h.h"
#include "battle/stg_a.h"
#include "battle/stg_d.h"
#include "battle/stgm_a.h"
#include "xb_disc.h"
#include "sys/snd.h"
#include "sys/file.h"
#include "xb_adxt.h"
#include "xb_sfx.h"
#include "xsdk/audio.h"
#include "sys/gfxm_b_b.h"
#include "sys/vu1_packet.h"
#include "battle/eft_ae.h"
#include "sys/pad.h"
#include "battle/battle_setup.h"
#include "sys/heap.h"
#include "sys/save.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <math.h>
/* Host test SDK device: validates PCM frames; no physical sound output. */
static const char *image_path;static FILE *image;
NTSTATUS NtCreateFile(HANDLE *h,uint32_t access,const OBJECT_ATTRIBUTES *a,IO_STATUS_BLOCK *io,const uint64_t *size,uint32_t flags,uint32_t share,uint32_t disp,uint32_t options){
 (void)access;(void)size;(void)flags;(void)share;(void)disp;(void)options;assert(a && a->name && io && !image);
 if(strcmp(a->name->pointer,"game:\\test.iso"))return STATUS_INVALID_PARAMETER;
 image=fopen(image_path,"rb");if(!image)return STATUS_INVALID_PARAMETER;*h=1;io->status=STATUS_SUCCESS;io->information=0;return STATUS_SUCCESS;
}
NTSTATUS NtClose(HANDLE h){assert(h==1 && image);fclose(image);image=NULL;return STATUS_SUCCESS;}
NTSTATUS NtQueryInformationFile(HANDLE h,IO_STATUS_BLOCK *io,void *out,uint32_t n,uint32_t type){
 assert(h==1 && image && n==sizeof(FILE_NETWORK_OPEN_INFORMATION) && type==FILE_NETWORK_OPEN_INFORMATION_CLASS);
 FILE_NETWORK_OPEN_INFORMATION *info=out;memset(out,0,n);assert(!fseeko(image,0,SEEK_END));info->end_of_file=(uint64_t)ftello(image);io->information=n;return STATUS_SUCCESS;
}
NTSTATUS NtReadFile(HANDLE h,HANDLE event,void *apc,void *ctx,IO_STATUS_BLOCK *io,void *out,uint32_t n,const uint64_t *at){
 assert(h==1 && image && !event && !apc && !ctx && at);if(fseeko(image,(off_t)*at,SEEK_SET))return STATUS_INVALID_PARAMETER;io->information=(uint32_t)fread(out,1,n,image);return STATUS_SUCCESS;
}
static float pcm[XSDK_AUDIO_FRAME_FLOATS];static unsigned device_open,pending,frames,played,nonzero;
NTSTATUS xsdk_audio_open(uint32_t n){assert(n==8 && !device_open);device_open=1;played=0;return STATUS_SUCCESS;}
NTSTATUS xsdk_audio_close(void){assert(device_open && !pending);device_open=0;return STATUS_SUCCESS;}
NTSTATUS xsdk_audio_next_frame(uint32_t ms,float **out){assert(!ms && device_open && !pending);pending=1;*out=pcm;return STATUS_SUCCESS;}
void xsdk_audio_queue_frame(void){assert(device_open && pending);pending=0;frames++;played++;for(unsigned i=0;i<XSDK_AUDIO_FRAME_FLOATS;i++){assert(isfinite(pcm[i]));if(i<512){assert(pcm[i]>=-1 && pcm[i]<=1);nonzero+=pcm[i]!=0;}else assert(pcm[i]==0);}}
void xsdk_audio_stats(XsdkAudioStats *out){memset(out,0,sizeof(*out));out->played=played;}
extern s32 *BtlScene_GetCommonEntry(s32);
extern void EftTexSet_Load32(EftAeTexSet32 *,void *);
extern AiThMgr *gBtlAi;
extern void BtlChar_ResetAll(void),BtlAiMgr_Reset(void);
extern void BtlChar_SetFlag(void *,int);
extern s32 Heap_GetFreeSize(void);
extern float D_002F2420[213];
void xsdk_log_write(const char *fmt,...){va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);fputc('\n',stderr);va_end(ap);}
static void *load(const char *root,const char *name,size_t *n){char path[2048];assert(snprintf(path,sizeof(path),"%s/%s",root,name)<(int)sizeof(path));FILE *f=fopen(path,"rb");assert(f && !fseek(f,0,SEEK_END));long s=ftell(f);assert(s>0);rewind(f);*n=(size_t)s;void *p=malloc(*n);assert(p && fread(p,1,*n,f)==*n);fclose(f);return p;}
static int read_program(void *root,const char *name,uint8_t **bytes,size_t *n){*bytes=load(root,!strcmp(name,"DBZP.BIN")?"BIN/DBZP.BIN":name,n);return 0;}
static uint32_t le32(const void *address){const uint8_t *p=address;return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void stage_oracle(const uint8_t *pack){
 size_t offset=le32(pack+8),length=le32(pack+12)-offset;void *copy=malloc(length);assert(copy);memcpy(copy,pack+offset,length);
 BtlStage *native=gBtlStage,oracle={0};gBtlStage=&oracle;BtlStage_BindData(&oracle,copy);gBtlStage=native;
 static const unsigned stride[20]={64,16,32,16,32,32,32,16,16,16,32,24,24,4,12,8,4,8,4,24};
 for(unsigned i=0;i<20;i++){
  const uint8_t *a=(const uint8_t *)native->data+8+i*8,*b=(const uint8_t *)oracle.data+8+i*8;uint32_t na,nb;void *pa,*pb;
  memcpy(&na,a,4);memcpy(&nb,b,4);memcpy(&pa,a+4,4);memcpy(&pb,b+4,4);assert(na==nb && !memcmp(pa,pb,(size_t)na*stride[i]));
 }
 free(copy);puts("PASS 20 MAPD sections against original binder");
}
static void stage_rejections(const uint8_t *pack,size_t size){
 size_t param=le32(pack+8),nav=le32(pack+64),path=le32(pack+96);
 size_t locations[]={param+8,param+20,param+44,param+176+12,nav+4,nav+16+32,path+32+4};
 uint32_t values[]={UINT32_MAX,0,UINT32_MAX,0,257,0x7fff7fff,UINT32_MAX};
 for(unsigned i=0;i<7;i++){
  uint8_t *copy=malloc(size);assert(copy);memcpy(copy,pack,size);for(unsigned j=0;j<4;j++)copy[locations[i]+j]=(uint8_t)(values[i]>>(8*j));
  XbBattleStageCollision *c=NULL;assert(!XbBattleStage_PrepareCollision(copy,size,&c));assert(XbBattleStage_PrepareParameters(c,copy,size)<0);assert(!XbBattleStage_FreeCollision(c));free(copy);
 }
 puts("PASS seven corrupt stage parameter/path/nav fixtures rejected");
}
static void geometry_rejections(const uint8_t *pack,size_t size){
 size_t stage=le32(pack+4),model=stage+le32(pack+stage+28)*4;
 size_t material=stage+le32(pack+model+4)*4,mesh=stage+le32(pack+material+4)*4;
 size_t root=stage+le32(pack+stage+80)*4,node=stage+le32(pack+stage+52)*4;
 size_t loc[]={stage+76,model+4,mesh+8,node+64,root+4};
 uint32_t val[]={0x7f800000,UINT32_MAX,UINT32_MAX,UINT32_MAX,(uint32_t)((root-stage)/4)};
 for(unsigned i=0;i<5;i++){
  XbBattleStageCollision *c=NULL;assert(!XbBattleStage_PrepareCollision(pack,size,&c));assert(!XbBattleStage_PrepareParameters(c,pack,size));
  uint8_t *copy=malloc(size);assert(copy);memcpy(copy,pack,size);for(unsigned j=0;j<4;j++)copy[loc[i]+j]=(uint8_t)(val[i]>>(8*j));
  assert(XbBattleStage_PrepareGeometry(c,copy,size,NULL)<0);assert(!XbBattleStage_FreeCollision(c));free(copy);
 }
 puts("PASS five corrupt geometry/root/mesh/key/cycle fixtures rejected");
}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static void stage_animation_rejections(const char *resources){
 size_t bytes;uint8_t *pack=load(resources,"../stages/stage-04.bin",&bytes),*copy=malloc(bytes);assert(copy);
 uint32_t anim=le32(pack+36),node=anim+le32(pack+anim+20),track=anim+le32(pack+anim+24);
 uint32_t first=node+le32(pack+node),mapd=le32(pack+8),defs=mapd+le32(pack+mapd+12);
 for(unsigned test=0;test<8;test++){
  memcpy(copy,pack,bytes);
  if(test==0)put32(copy+anim+20,(uint32_t)bytes);
  if(test==1)put32(copy+first,0);
  if(test==2)put32(copy+first+4,65);
  if(test==3)put32(copy+first+16,0x7fc00000);
  if(test==4){copy[track+10]=255;copy[track+11]=255;}
  if(test==5){copy[track+12]=0;copy[track+13]=0;}
  if(test==6)put32(copy+track+16,0x7fc00000);
  if(test==7)put32(copy+defs+11*64+8,65535);
  XbBattleStageCollision *c=NULL;assert(!XbBattleStage_PrepareCollision(copy,bytes,&c));assert(!XbBattleStage_PrepareStarts(c,copy,bytes));
  int e=XbBattleStage_PrepareParameters(c,copy,bytes);if(!e)e=XbBattleStage_PrepareGeometry(c,copy,bytes,NULL);assert(e<0);assert(!XbBattleStage_FreeCollision(c));
 }
 free(copy);free(pack);puts("PASS eight malformed real-stage animation fixtures rejected without publication");
}
static void verify_character_effect_preparation(const char *resources){
 size_t n;uint8_t *pack=load(resources,"left-effects.bin",&n),*bad=malloc(n);assert(bad);
 uint32_t root=le32(pack+24),flags=root+le32(pack+root+4),emitter=root+le32(pack+root+8);
 uint32_t head=emitter+le32(pack+emitter+4),tex=emitter+le32(pack+emitter+8);
 uint32_t spritepack=root+le32(pack+root+24),sprite=spritepack+le32(pack+spritepack+4);
 uint32_t camera_pack=root+le32(pack+root+32),camera=camera_pack+le32(pack+camera_pack+8);
 uint32_t channel=camera+le32(pack+camera+8),keys=camera+le32(pack+channel+4);
 uint32_t technique=le32(pack+4),tech_head=technique+le32(pack+technique+16);
 uint32_t technique3=le32(pack+16),extra=technique3+le32(pack+technique3+12);
 uint32_t places[]={0,root,flags+4,head+4,emitter+8,sprite+8,channel,keys+4,tex+32,tech_head+4,extra+4};
 uint32_t values[]={5,12,4,255,UINT32_MAX,UINT32_MAX,8,0x7fc00000,UINT32_MAX,255,0x7fc00000};
 for(unsigned i=0;i<11;i++){
  memcpy(bad,pack,n);put32(bad+places[i],values[i]);XbCharacterEffects *fx=(void *)1;XbCharacterEffectsInfo info;
  assert(XbCharacterEffects_Prepare(bad,n,&fx,&info)<0 && !fx && !info.manager_entries);
 }
 XbCharacterEffects *fx=NULL;XbCharacterEffectsInfo info;
 assert(!XbCharacterEffects_Prepare(pack,n,&fx,&info));assert(info.emitter_sets==9 && info.prepared_technique_packs==5 && info.unprepared_technique_packs==0);
 for(unsigned i=0;i<5;i++)assert(XbCharacterEffects_TechniqueRoot(fx,i));
 extern void verify_character_effect_consumers(void *);verify_character_effect_consumers(XbCharacterEffects_Root(fx));
 XbCharacterEffects_Free(fx);free(bad);free(pack);
 puts("PASS eleven malformed character effect fixtures rejected without publication");
}
static void verify_stage_converters(const char *resources){
 unsigned animated=0,timers=0,failed=0;
 for(unsigned i=0;i<39;i++){
  char name[80];snprintf(name,sizeof(name),"../stages/stage-%02u.bin",i);size_t bytes;void *pack=load(resources,name,&bytes);
  XbBattleStageCollision *c=NULL;int e=XbBattleStage_PrepareCollision(pack,bytes,&c);
  if(e)printf("collision fail %d\n",e);
  if(!e){e=XbBattleStage_PrepareStarts(c,pack,bytes);if(e)printf("starts fail %d\n",e);}
  if(!e){e=XbBattleStage_PrepareParameters(c,pack,bytes);if(e)printf("params fail %d\n",e);}
  XbStageGeometryInfo info={0};if(!e)e=XbBattleStage_PrepareGeometry(c,pack,bytes,&info);
  printf("real stage %02u conversion: %d; timers=%u meshes=%u\n",i,e,info.timers,info.meshes);if(e)failed++;
  unsigned off=le32((const uint8_t *)pack+36),end=le32((const uint8_t *)pack+40);animated+=end>off;timers+=info.timers;
  assert(!XbBattleStage_FreeCollision(c));free(pack);
 }
 assert(!failed);printf("PASS 39 real stage converters: %u animated packs, %u timers; no stage draw\n",animated,timers);
}
static void verify_stage_cycles(XbBattleRuntime *runtime,XbBattleStageCollision *original,const char *resources){
 extern void *gStgAnimTable;
 assert(!XbBattleRuntime_EndStage(runtime));StgNav_Term();
 assert(!XbBattleStage_UninstallCollision(original));
 unsigned animated_objects=0,timer_wraps=0;
 for(unsigned stage=0;stage<39;stage++){
  char name[80];snprintf(name,sizeof(name),"../stages/stage-%02u.bin",stage);size_t bytes;void *pack=load(resources,name,&bytes);
  XbBattleStageCollision *c=NULL;XbStageGeometryInfo info={0};
  assert(!XbBattleStage_PrepareCollision(pack,bytes,&c));assert(!XbBattleStage_PrepareStarts(c,pack,bytes));
  assert(!XbBattleStage_PrepareParameters(c,pack,bytes));assert(!XbBattleStage_PrepareGeometry(c,pack,bytes,&info));
  free(pack);assert(!XbBattleStage_InstallCollision(c));StgNav_Init();
  printf("entering real stage %u original initialization\n",stage);
  int result=XbBattleRuntime_BeginStage(runtime,c);printf("real stage %u original init result %d\n",stage,result);assert(!result && BtlStage_IsReady());
  if(gStgAnimTable){
   StgOfsTable *table=gStgAnimTable;Mtx44 identity __attribute__((aligned(16)))={0};for(unsigned k=0;k<4;k++)identity.m[k][k]=1;
   for(int k=0;k<table->count;k+=3){
    uint8_t *block=(void *)StgOfsTable_Get(table,k+1);uint32_t offset;memcpy(&offset,block,4);
    Vu1Node *node=(Vu1Node *)(block+offset);Vu1Track *track=(void *)StgOfsTable_Get(table,k+2);
    for(unsigned frame=0;frame<3;frame++){
     Vu1Node_Animate(node,track,&identity,(float)(frame*5));
     for(Vu1Node *p=node;;p=(Vu1Node *)((uint8_t *)p+p->size)){for(unsigned j=0;j<16;j++)assert(isfinite(p->world[j]));if(p->last)break;}
    }
   }
  }
  unsigned before_animated=animated_objects;
  float initial[256]={0};assert(gBtlStage->data->objCount<=256);
  for(int i=0;i<gBtlStage->data->objCount;i++){
   StgObjDef *d=&gBtlStage->data->objDefs[i];memcpy(&initial[i],(uint8_t *)d+28,4);
   if(d->unk8>0){animated_objects++;assert(gStgAnimTable);}
  }
  for(unsigned frame=0;frame<12;frame++){
   assert(!XbBattleRuntime_ServiceStage(runtime));
   for(unsigned i=0;i<info.timers;i++){unsigned short flags;memcpy(&flags,(uint8_t *)gBtlStage->timers+i*16,2);timer_wraps+=(flags&4)!=0;}
  }
  if(animated_objects>before_animated){unsigned changed=0;
   for(int i=0;i<gBtlStage->data->objCount;i++){float value;memcpy(&value,(uint8_t *)&gBtlStage->data->objDefs[i]+28,4);if(gBtlStage->data->objDefs[i].unk8>0){assert(isfinite(value));changed+=value!=initial[i];}}
   assert(changed);
  }
  StgView view={0};for(unsigned i=0;i<4;i++)view.mtx.m[i][i]=1;view.x1=640;view.screenDist=256;view.nearZ=1;view.farZ=100000;
  StgModel_Cull(&view);
  assert(!XbBattleRuntime_EndStage(runtime));StgNav_Term();
  assert(!XbBattleStage_UninstallCollision(c));assert(!XbBattleStage_FreeCollision(c));
  printf("PASS real stage %u original Init/12 Updates/Cull/Term; no GPU draw\n",stage);
 }
 assert(animated_objects && timer_wraps);
 printf("PASS real stage cycles: 39 stages, %u animated object definitions, %u timer wraps\n",animated_objects,timer_wraps);
 assert(!XbBattleStage_InstallCollision(original));StgNav_Init();
}
static void common3_rejections(XbBattleRuntime *runtime,const uint8_t *pack,size_t size){
 size_t set=le32(pack+0xc*4),param=le32(pack+14*4),sprite=le32(pack+29*4),tex=le32(pack+3*4),mesh=le32(pack+0x239*4);
 size_t extra_d=le32(pack+0x23d*4),extra_e=le32(pack+0x23e*4);
 size_t where[]={0,set+64,param+0x28,sprite+8,tex+32+64+4,mesh+le32(pack+mesh+8),set+64+pack[set+4]*32+1,extra_d+56,extra_e+8};
 uint32_t what[]={638,19,0x7fc00000,UINT32_MAX,UINT32_MAX,UINT32_MAX,255,2,0x7fc00000};unsigned width[]={4,1,4,4,4,4,1,4,4};
 for(unsigned i=0;i<9;i++){
  uint8_t *copy=malloc(size);assert(copy);memcpy(copy,pack,size);for(unsigned k=0;k<width[i];k++)copy[where[i]+k]=(uint8_t)(what[i]>>(k*8));
  XbCommon3Info info={0};assert(XbBattleRuntime_AttachCommon3(runtime,copy,size,&info)<0 && info.prepared==0);
  void *out=(void *)1;assert(!XbBattleRuntime_TryCommon3Entry(1,&out));free(copy);
 }
 puts("PASS nine corrupt common3 set/parameter/sprite/palette/mesh/scalar fixtures rejected without commit");
}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IOLBF,0);assert(argc==4);
 image_path=argv[3];assert(!XbDisc_Mount("game:/test.iso"));
 XbDataFiles files={read_program,argv[2]};int rc=XbTypedData_Load(&files,&gXbTypedMainCatalog,0);printf("typed data: %d\n",rc);assert(!rc && gPortDataStripped==2);
 size_t n2,n4,ns;void *c2=load(argv[1],"common-2.bin",&n2),*c4=load(argv[1],"common-4.bin",&n4),*stage=load(argv[1],"stage.bin",&ns);
 XbBattleRuntime *runtime=NULL;rc=XbBattleRuntime_Open(c2,n2,c4,n4,&runtime);printf("runtime: %d\n",rc);assert(!rc);free(c2);free(c4);
 size_t n3;void *c3=load(argv[1],"common-3.bin",&n3);XbCommon3Info common3;common3_rejections(runtime,c3,n3);rc=XbBattleRuntime_AttachCommon3(runtime,c3,n3,&common3);printf("common3 preparation: %d entries=%u textures=%u images=%u unprepared=%u\n",rc,common3.entries,common3.textures,common3.images,common3.unprepared);assert(!rc);free(c3);
 for(unsigned i=1;i<=637;i++){if(XbBattleRuntime_Common3Type(i)!=1)continue;TexFile *texture=(TexFile *)BtlScene_GetCommonEntry((s32)i);if(!texture)continue;TexFile *relocated=NULL;Res_RelocateOffsets(&relocated,(u8 *)texture,texture);assert(relocated==texture && texture->ent && texture->count<=32);EftAeTexSet32 set;EftTexSet_Load32(&set,texture);assert(set.count==(int)texture->count);for(unsigned j=0;j<texture->count;j++){assert((texture->ent[j].pix || texture->ent[j].clut) && (void *)set.tex[j].image==&texture->ent[j] && set.tex[j].tex0==texture->ent[j].tex0);}}
 assert(!BtlScene_GetCommonEntry(-1) && !BtlScene_GetCommonEntry(638));
 assert(common3.entries==637 && common3.prepared==637 && common3.unprepared==0);
 assert(XbBattleRuntime_Common3Type(0x23d)==6 && XbBattleRuntime_Common3Type(0x23e)==6);
 float extra_value;memcpy(&extra_value,(uint8_t *)BtlScene_GetCommonEntry(0x23d)+8,4);assert(extra_value==130.0f);
 uint32_t extra_flag;memcpy(&extra_flag,(uint8_t *)BtlScene_GetCommonEntry(0x23d)+56,4);assert(extra_flag==1);
 memcpy(&extra_value,BtlScene_GetCommonEntry(0x23e),4);assert(extra_value==209.0f);
 extern void verify_common3_consumers(void);verify_common3_consumers();verify_character_effect_preparation(argv[1]);stage_animation_rejections(argv[1]);verify_stage_converters(argv[1]);
 s32 heap=Heap_GetFreeSize();
 XbBattleStageCollision *collision=NULL;assert(!XbBattleStage_PrepareCollision(stage,ns,&collision));assert(!XbBattleStage_PrepareStarts(collision,stage,ns));rc=XbBattleStage_PrepareParameters(collision,stage,ns);printf("stage parameters: %d\n",rc);assert(!rc);XbStageGeometryInfo geometry;rc=XbBattleStage_PrepareGeometry(collision,stage,ns,&geometry);printf("stage geometry prepare: %d groups=%u materials=%u meshes=%u parts=%u nodes=%u cells=%u textures=%u\n",rc,geometry.groups,geometry.materials,geometry.meshes,geometry.parts,geometry.nodes,geometry.cells,geometry.textures);assert(!rc);stage_rejections(stage,ns);geometry_rejections(stage,ns);assert(!XbBattleStage_InstallCollision(collision));stage_oracle(stage);free(stage);assert(gBtlStage->data && gBtlStage->data->objCount==11 && !BtlStage_IsReady() && BtlStage_GetPathCount()==5);
 for(unsigned pass=0;pass<2;pass++){
  if(pass){size_t bytes;void *pack=load(argv[1],"stage.bin",&bytes);assert(!XbBattleStage_PrepareGeometry(collision,pack,bytes,NULL));free(pack);}
  assert(!XbTypedData_Load(&files,&gXbTypedMainCatalog,XBTD_VALIDATE_ONLY));assert(Heap_GetFreeSize()==heap);
  assert(!XbBattleFighter_BeginOriginalObjects());XbBattleFighter *fighters[2];
  for(unsigned side=0;side<2;side++){
   size_t mn,an;void *model=load(argv[1],side?"right-model.bin":"left-model.bin",&mn),*anim=load(argv[1],side?"right-animation.bin":"left-animation.bin",&an);XbFighterPrepared info;
   assert(!XbBattleFighter_PrepareModelAnimation(model,mn,anim,an,&fighters[side],&info));free(model);free(anim);
   size_t fx_bytes;void *fx_pack=load(argv[1],side?"right-effects.bin":"left-effects.bin",&fx_bytes);XbCharacterEffectsInfo fx_info;
   int fx_error=XbBattleFighter_AttachEffectManagers(fighters[side],fx_pack,fx_bytes,&fx_info);free(fx_pack);
   printf("character effects side=%u error=%d sets=%u textures=%u sprites=%u cameras=%u techniques=%u/%u\n",side,fx_error,fx_info.emitter_sets,fx_info.textures,fx_info.sprite_packs,fx_info.cameras,fx_info.prepared_technique_packs,fx_info.unprepared_technique_packs);assert(!fx_error);
   assert(fx_info.prepared_technique_packs==5 && fx_info.unprepared_technique_packs==0);
   assert(!XbBattleFighter_BindCommonReference(fighters[side],D_002F2420,213));int id=-1;
   assert(!XbBattleFighter_CreateModelObject(fighters[side],side,&id) && id==(int)side);
  }
  puts("objects ready; entering original roster/AI reset");
  rc=XbBattleRuntime_BeginManagers(runtime);printf("original managers: %d\n",rc);assert(!rc);
  assert(gBtlChars && gBtlChars->count==2 && gBtlAi && gBtlAi->data);
  assert(gStgNav && gStgNav->ready && gStgNav->data->count==165 && !StgNav_IsReady());
  assert(XbBattleStage_UninstallCollision(collision)==XB_STAGE_STATE);
  for(int i=0;i<165;i++){StgColVec pos;memcpy(&pos,&gStgNav->data->nodes[i].pos,16);int nearest=StgNav_FindNearest(&pos);if(!(gStgNav->data->nodes[i].flags&1))assert(nearest>=0 && nearest<165);}
  XbBattleRuntimeInfo runtime_info;assert(!XbBattleRuntime_GetInfo(runtime,&runtime_info));
  assert(runtime_info.health[0]==40000 && runtime_info.health[1]==40000 && runtime_info.ai_level[1]==13);
  assert(XbBattleRuntime_BeginManagers(runtime)<0);
  for(unsigned side=0;side<2;side++){
   BtlMgrChr *chr=&gBtlChars->chars[side];assert(chr->side==(int)side && chr->objId==(int)side && chr->memberCount==1);
   assert(chr->members[0].present && chr->members[0].chara==(int)side && chr->members[0].cpuLevel==13);
   assert(chr->members[0].gauge.health>0 && chr->members[0].gauge.health==chr->members[0].gauge.healthMax);
   assert(gBtlAi->work[side].objId==(int)side && gBtlAi->work[side].param && gBtlAi->work[side].level==13);
   printf("side=%u hp=%d ki=%d AI=%d level=%d\n",side,chr->members[0].gauge.health,chr->members[0].gauge.ki,gBtlAi->work[side].type,gBtlAi->work[side].level);
  }
  assert(isfinite(gBtlAi->radiusSum) && gBtlAi->radiusSum>0);
  const char *names[4]={"sound-common.bin","sound-stage.bin","voice-left.bin","voice-right.bin"};void *sound[4];size_t sizes[4];
  for(unsigned i=0;i<4;i++)sound[i]=load(argv[1],names[i],&sizes[i]);
  XbSoundBank *invalid=(void *)1;
  assert(XbSfx_PrepareBank(sound[0],19,4,&invalid)<0 && !invalid);
  assert(XbSfx_PrepareBank(sound[0],sizes[0],3,&invalid)<0 && !invalid);
  assert(XbSfx_PrepareBank(sound[0],sizes[0],8,&invalid)<0 && !invalid);
  uint8_t *bad=malloc(sizes[0]);assert(bad);memcpy(bad,sound[0],sizes[0]);memset(bad+16,0xff,4);
  assert(XbSfx_PrepareBank(bad,sizes[0],4,&invalid)<0 && !invalid);free(bad);
  int maximum[4];uint8_t loops[4][19]={{0}};
  for(unsigned i=0;i<4;i++){maximum[i]=SndHd_GetVagMax(sound[i]);SndHd_GetLoopBits(sound[i],loops[i]);}
  rc=XbBattleRuntime_BeginAudio(runtime,(const void *const *)sound,sizes);printf("original audio load: %d\n",rc);assert(!rc);
  extern SndMgr *gSndMgr;
  for(unsigned i=0;i<4;i++)assert(gSndMgr->bank[i+2].vagMax==maximum[i] && !memcmp(gSndMgr->bank[i+2].loopBits,loops[i],19));
  for(unsigned i=0;i<4;i++)free(sound[i]);assert(Snd_IsUploadDone());unsigned before=nonzero;
  rc=XbBattleRuntime_BeginStage(runtime,collision);printf("original stage init: %d ready=%d\n",rc,BtlStage_IsReady());assert(!rc && BtlStage_IsReady() && gStgRigid);
  if(getenv("XB_TEST_FULL_SCENE")){
   puts("entering original full effect scene init");
   assert(!XbBattleRuntime_BeginScene(runtime));puts("original camera and effect scene init returned");
   if(getenv("XB_TEST_SCENE_CHAR")){
    assert(!XbBattleRuntime_BeginSceneCharacters(runtime));puts("original effect characters created");
    assert(gEftShot && gEftShot->chr && gEftShot->count==2);
    extern EftShotClassRow gEftShotClass[11];
    for(unsigned side=0;side<2;side++){
     assert(gEftShot->chr[side].task);
     for(unsigned slot=0;slot<5;slot++){
      int converted=1;
      EftHSlot *s=&((EftHChar *)gEftShot->chr)[side].slot[slot];
      assert(!!s->pack==converted && s->param);
      if(converted){
       int row=s->param->type+1;assert(row>=0 && row<11);
       if(gEftShotClass[row].group)assert(s->task);
      }
     }
    }
    puts("original effect slots: 10 prepared packs bound");
   }
   if(getenv("XB_TEST_SCENE_UPDATE")){
    for(int frame=0;frame<3;frame++){assert(!XbBattleRuntime_ServiceScene(runtime));printf("original effect scene frame %d returned\n",frame);}
   }
   if(getenv("XB_TEST_SCENE_CHAR")){
    assert(!XbBattleRuntime_EndSceneCharacters(runtime));puts("original effect characters freed");
   }
   assert(!XbBattleRuntime_EndScene(runtime));puts("original full effect scene term returned");
  }
  assert(StgNav_IsReady());
  /* An adjacent graph edge has a short, independently known route. */
  int start=-1,goal=-1;
  for(int i=0;i<gStgNav->data->count && start<0;i++){
   StgNavNode *n=&gStgNav->data->nodes[i];if(n->flags&STGNAV_NODE_OFF)continue;
   for(unsigned j=0;j<8;j++)if(n->link[j]>=0 && !(gStgNav->data->nodes[n->link[j]].flags&STGNAV_NODE_OFF)){start=i;goal=n->link[j];break;}
  }
  assert(start>=0 && goal>=0);StgColVec from;StgNavPoint to __attribute__((aligned(16)));StgNavPath path;
  memcpy(&from,&gStgNav->data->nodes[start].pos,16);to=gStgNav->data->nodes[goal].pos;
  StgNav_FindPath(&from,&to,&path);assert(path.count>=2 && path.count<=3 && !memcmp(&path.pts[0],&to,16));
  printf("PASS original navigation route: %d -> %d, %d points\n",start,goal,path.count);
  Vec4 bodypos={0,-100,0,1},push={2,0,0,0},spin={0.1f,0,0,0};
  int body=StgRigid_Create(&bodypos,1,0);assert(body>0);StgRigid_LaunchSpin(body,&push,&spin);
  float before_y=StgRigid_Find(body)->body.pos.y;
  for(unsigned i=0;i<12;i++)assert(!XbBattleRuntime_ServiceStage(runtime));
  StgRigidNode *node=StgRigid_Find(body);assert(node && isfinite(node->body.pos.y) && node->body.pos.y!=before_y && gStgRigid->count==1);
  Mtx44 bodymatrix;assert(StgRigid_GetMatrix(&bodymatrix,body));
  printf("PASS original rigid body: y=%f -> %f\n",(double)before_y,(double)node->body.pos.y);
  StgRigid_Release(body);assert(!StgRigid_Find(body));
  /* Stage 0 has zero timers: separately exercise the original timer consumer. */
  struct {unsigned short flags,unused;int pad;float time,period;} timer={0,0,0,0,0.2f};
  assert(!gBtlStage->timerCount);void *old_timers=gBtlStage->timers;gBtlStage->timers=&timer;gBtlStage->timerCount=1;
  extern void BtlStage_UpdateTimers(void);BtlStage_UpdateTimers();assert(timer.time>0 && !(timer.flags&4));
  BtlStage_UpdateTimers();assert(timer.time==0 && (timer.flags&4));gBtlStage->timerCount=0;gBtlStage->timers=old_timers;
  StgView view={0};for(unsigned i=0;i<4;i++)view.mtx.m[i][i]=1;view.x1=640;view.screenDist=256;view.nearZ=1;view.farZ=100000;
  StgModel_Cull(&view);BtlStageM *graphics=(BtlStageM *)gBtlStage;unsigned visible=0;
  for(unsigned i=0;i<graphics->visSize;i++)visible+=graphics->vis[i]!=0;assert(visible>0 && visible<=2243);
  printf("PASS original stage timers and CPU culling: %u visible meshes; no GPU draw\n",visible);
  Vec4 pos __attribute__((aligned(16))),rot __attribute__((aligned(16)));int zone=BtlStage_GetStartPlace(0,&pos,&rot,0);assert(zone>=0 && isfinite(pos.y));
  printf("PASS original stage relocation/init/reset/update: zone=%d y=%f\n",zone,(double)pos.y);

  /* Restart stage/file players without closing the active audio device. */
  assert(!XbBattleRuntime_EndStage(runtime));size_t restart_bytes;void *restart=load(argv[1],"stage.bin",&restart_bytes);
  assert(!XbBattleStage_PrepareGeometry(collision,restart,restart_bytes,NULL));free(restart);
  assert(!XbBattleRuntime_BeginStage(runtime,collision));assert(!XbBattleRuntime_ServiceStage(runtime));
  if(!pass)verify_stage_cycles(runtime,collision,argv[1]);
  assert(Snd_QueuePlay(4,0,100,64,0,0)>=0 && Snd_QueuePlay(16,0,100,64,0,0)>=0 && Snd_QueuePlay(32,0,100,64,0,0)>=0);
  for(unsigned i=0;i<12;i++)assert(!XbBattleRuntime_ServiceAudio(runtime));assert(nonzero>before && frames>0 && !XbSfx_LastError());
  assert(!XbBattleRuntime_EndAudio(runtime) && !device_open);printf("PASS original audio: %u frames, %u nonzero samples; SDK device simulated\n",frames,nonzero);

  /* Isolated original input phase: a physical-pad fixture, then release.
   * Flag 2 normally comes from the battle sequence; no sequence is claimed. */
  BtlInputChr *input=(BtlInputChr *)&gBtlChars->chars[0];
  BtlChar_SetFlag(input,2);gPad[0].held=PAD_SQUARE;gPad[0].status=0;gPad[0].left[0]=0.5f;gPad[0].left[1]=0;
  BtlInput_Sample(input);assert(input->input.rec.buttons&BTLB_RUSH);
  BtlInput_Update(input);assert(input->input.held&BTLB_RUSH);assert(input->input.pressed&BTLB_RUSH);
  gPad[0].held=0;BtlInput_Sample(input);BtlInput_Update(input);assert(input->input.released&BTLB_RUSH);
  puts("PASS original input sample/remap/press/release with physical-pad fixture");
  BtlChar_ResetAll();BtlAiMgr_Reset();
  assert(!XbBattleRuntime_EndManagers(runtime) && !gBtlChars && !gBtlAi && !gSaveData);
  for(unsigned side=0;side<2;side++)XbBattleFighter_FreeModelAnimation(fighters[side]);
  assert(!XbBattleFighter_EndObjectDiagnostic());assert(Heap_GetFreeSize()==heap);
  printf("PASS original roster reset / AI init / reset / terminate: pass=%u heap restored\n",pass);
 }
 assert(!XbBattleStage_UninstallCollision(collision));assert(!XbBattleStage_FreeCollision(collision));assert(!XbBattleRuntime_Close(runtime));
 XbDisc_Unmount();puts("PASS no Battle_Update/Battle_Main or hardware execution; audio SDK simulated; marker stays 2");return 0;
}
