/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Included by xb_battle_stage.c. Native metadata, never render-stage readiness.
 * Schemas: stg_a.h, stg_b.h, eft_c.h, eft_z.h and gfxm_a.h. */
typedef struct ParamSchema {uint8_t stride;uint16_t words,negate;uint32_t halves;} ParamSchema;
static const ParamSchema param_schema[20]={
 {64,0xff8f,0x600,0},{16,15,4,0},{32,255,0x66,0},{16,15,6,0},
 {32,255,0x66,0},{32,255,0x66,0},{32,255,0x66,0},{16,1,0,0},
 {16,3,0,0},{16,1,0,0},{32,31,6,1u<<10},
 {24,31,6,(1u<<10)|(1u<<11)},{24,0x19,0,1u<<10},
 {4,1,0,0},{12,0,0,0},{8,0,0,0},{4,0,0,0},{8,1,0,0},
 {4,1,0,0},{24,0x21,0,0}
};
static int param_span(size_t size,uint32_t at,size_t count,size_t stride,size_t min){
 return at<min || (at&3) || at>size || count>(size-at)/stride;
}
static int param_finite(const uint8_t *p,unsigned n){for(unsigned k=0;k<n;k++)if(!isfinite(rf(p+k*4)))return DR_STAGE_NUMBER;return 0;}
static void param_halves(void *to,const uint8_t *from,size_t n){for(size_t k=0;k<n;k+=2){uint16_t v=(uint16_t)(from[k]|(unsigned)from[k+1]<<8);memcpy((uint8_t *)to+k,&v,2);}}
int XbBattleStage_PrepareParameters(XbBattleStageCollision *c,const void *pack,size_t n){
 if(!c || !pack || c->installed || c->parameters || sizeof(void *)!=4)return XB_STAGE_STATE;
 size_t at,bytes,start[20],end[20];unsigned flags;int e=DR_STAGE_FORMAT;
 if(dr_pack_memory_entry(pack,n,1,&at,&bytes,&flags) || flags || bytes<168 || bytes>0x100000)return e;
 const uint8_t *p=(const uint8_t *)pack+at;
 if(rd(p)!=0x4450414d || rd(p+4))return e;
 uint8_t *params=malloc(bytes),*nav=NULL,*paths=NULL,*mef=NULL;if(!params)return XB_STAGE_MEMORY;
 memcpy(params,p,bytes);words(params,p,168);
 for(unsigned i=0;i<20;i++){
  uint32_t count=rd(p+8+i*8),off=rd(p+12+i*8);const ParamSchema *s=&param_schema[i];
  if(param_span(bytes,off,count,s->stride,168)){e=DR_STAGE_BOUNDS;goto fail;}
  start[i]=off;end[i]=off+(size_t)count*s->stride;
  for(unsigned j=0;j<i;j++)if(count && end[j]>start[j] && start[i]<end[j] && start[j]<end[i])goto fail;
  for(uint32_t j=0;j<count;j++){
   const uint8_t *src=p+off+(size_t)j*s->stride;uint8_t *dst=params+off+(size_t)j*s->stride;
   for(unsigned k=0;k<s->stride/4;k++)if(s->words&(1u<<k))words(dst+k*4,src+k*4,4);
   for(unsigned k=0;k<s->stride/2;k++)if(s->halves&(1u<<k))param_halves(dst+k*2,src+k*2,2);
   unsigned floats=i==2?8:i>=3&&i<=6?(i==3?4:8):i==10?5:i==11?4:0;
   if((floats && param_finite(src,floats)) || (i==0 && param_finite(src+32,8)) || (i==1 && param_finite(src+8,1)) || (i==12 && param_finite(src+12,2)) || (i==19 && param_finite(src+20,1))){e=DR_STAGE_NUMBER;goto fail;}
   for(unsigned k=0;k<s->stride/4;k++)if(s->negate&(1u<<k)){float f=-rf(src+k*4);memcpy(dst+k*4,&f,4);}
  }
  uintptr_t ptr=(uintptr_t)(params+off);memcpy(params+12+i*8,&ptr,4);
 }
 /* MAPD also includes animation-only objects without collision objects. */
 if(rd(p+8)<c->source.objects || rd(p+16)!=1 || rd(p+24)!=1 || rd(p+32)!=1 || rd(p+40)!=2 || rd(p+48)!=1 || rd(p+56)!=1)goto fail;
 for(uint32_t i=c->source.objects;i<rd(p+8);i++){
  /* Original animation consumers may query collision state unless this bit
   * is set. Extra MAPD objects must be animation-only, as in the real packs. */
  const uint8_t *def=p+start[0]+(size_t)i*64;
  if(!(rd(def+4)&0x40000000u) || (int32_t)rd(def+8)<=0){e=DR_STAGE_FORMAT;goto fail;}
 }
 for(uint32_t i=0;i<rd(p+8);i++){
  int32_t parent=(int32_t)i;uint32_t budget=rd(p+8)+1;
  while(parent>=0){if((uint32_t)parent>=rd(p+8) || !budget--){e=DR_STAGE_TREE;goto fail;}parent=(int32_t)rd(p+start[0]+(size_t)parent*64+12);}
 }
 if(dr_pack_memory_entry(pack,n,23,&at,&bytes,&flags) || flags || bytes<32)goto fail;
 p=(const uint8_t *)pack+at;uint32_t count=rd(p+8),off=rd(p+12);
 if(rd(p)!=0x70505645 || rd(p+4)!=1 || rd(p+16) || count>256 || param_span(bytes,off,count,16,32)){e=DR_STAGE_BOUNDS;goto fail;}
 paths=malloc(bytes);if(!paths){e=XB_STAGE_MEMORY;goto fail;}memcpy(paths,p,bytes);words(paths,p,32);
 uintptr_t ptr=(uintptr_t)(paths+off);memcpy(paths+12,&ptr,4);uint32_t one=1;memcpy(paths+16,&one,4);
 for(uint32_t i=0;i<count;i++){
  const uint8_t *q=p+off+i*16;uint32_t last=rd(q),point=rd(q+4),first=rd(q+8);
  if(first>=last || param_span(bytes,point,last,48,off+(size_t)count*16)){e=DR_STAGE_BOUNDS;goto fail;}
  words(paths+off+i*16,q,16);ptr=(uintptr_t)(paths+point);memcpy(paths+off+i*16+4,&ptr,4);
  for(uint32_t j=0;j<last;j++){if(param_finite(p+point+(size_t)j*48,3)){e=DR_STAGE_NUMBER;goto fail;}words(paths+point+(size_t)j*48,p+point+(size_t)j*48,12);}
 }
 e=dr_pack_memory_entry(pack,n,15,&at,&bytes,&flags);
 if(e==DR_NOT_FOUND && !c->source.objects){
  /* Preserve the original no-graph state; StgNav_Bind returns -2. */
  goto navigation_done;
 }
 if(e || flags || bytes<16)goto fail;
 p=(const uint8_t *)pack+at;count=rd(p+4);
 if(rd(p)!=1 || !count || count>256 || count>(bytes-16)/48){e=DR_STAGE_BOUNDS;goto fail;}
 nav=malloc(16+(size_t)count*48);if(!nav){e=XB_STAGE_MEMORY;goto fail;}words(nav,p,12);memset(nav+12,0,4);
 for(uint32_t i=0;i<count;i++){
  const uint8_t *q=p+16+(size_t)i*48;int32_t obj=(int32_t)rd(q+20);
  if(param_finite(q,4)){e=DR_STAGE_NUMBER;goto fail;}
  if(obj< -1 || (obj>=0 && (uint32_t)obj>=c->source.objects)){e=DR_STAGE_BOUNDS;goto fail;}
  words(nav+16+(size_t)i*48,q,32);param_halves(nav+48+(size_t)i*48,q+32,16);
  for(unsigned k=0;k<8;k++){int16_t link=(int16_t)(q[32+k*2]|(unsigned)q[33+k*2]<<8);if(link< -1 || link>=(int)count){e=DR_STAGE_BOUNDS;goto fail;}}
 }
navigation_done:
 e=dr_pack_memory_entry(pack,n,18,&at,&bytes,&flags);
 if(e!=DR_NOT_FOUND){
  if(e || flags || bytes<32){e=DR_STAGE_FORMAT;goto fail;}
  p=(const uint8_t *)pack+at;if(rd(p)!=0x3046454d){e=DR_STAGE_FORMAT;goto fail;}
  mef=malloc(bytes);if(!mef){e=XB_STAGE_MEMORY;goto fail;}memcpy(mef,p,bytes);words(mef,p,32);
  for(unsigned i=0;i<3;i++){
   count=rd(p+8+i*8);off=rd(p+12+i*8);if(param_span(bytes,off,count,48,32)){e=DR_STAGE_BOUNDS;goto fail;}
   ptr=(uintptr_t)(mef+off);memcpy(mef+12+i*8,&ptr,4);
   for(uint32_t j=0;j<count;j++){
    const uint8_t *q=p+off+(size_t)j*48;if(param_finite(q,3)){e=DR_STAGE_NUMBER;goto fail;}words(mef+off+(size_t)j*48,q,12);
    for(unsigned k=1;k<=2;k++){float f=-rf(q+4*k);memcpy(mef+off+(size_t)j*48+4*k,&f,4);}
   }
  }
 }
 c->parameters=params;c->nav_bytes=nav;c->path_bytes=paths;c->mef_bytes=mef;return 0;
fail:free(params);free(nav);free(paths);free(mef);return e?e:DR_STAGE_FORMAT;
}
static void publish_parameters(XbBattleStageCollision *c){
#if UINTPTR_MAX==UINT32_MAX
 if(c->parameters){
  extern StgPathTable *gStgPaths;extern StgMef *gBtlStageFxRes;
  memcpy(c->stage.header+4,&c->parameters,4);c->saved_paths=gStgPaths;c->saved_mef=gBtlStageFxRes;
  gStgPaths=(StgPathTable *)c->path_bytes;gBtlStageFxRes=(StgMef *)c->mef_bytes;c->parameters_bound=1;
 }
#else
 (void)c;
#endif
}
static void unpublish_parameters(XbBattleStageCollision *c){
#if UINTPTR_MAX==UINT32_MAX
 if(c->parameters_bound){
  extern StgPathTable *gStgPaths;extern StgMef *gBtlStageFxRes;
  gStgPaths=c->saved_paths;gBtlStageFxRes=c->saved_mef;memset(c->stage.header+4,0,4);c->parameters_bound=0;
 }
#else
 (void)c;
#endif
}
int XbStage_TryBindNav(void **out){if(!out || !active || !active->parameters_bound)return 0;*out=active->nav_bytes;return 1;}
int XbBattleStage_HasParameters(void){return active && active->parameters_bound;}
