/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_stage_pack.h"
#include "xb_resconv.h"
#include "xb_stage_camera.h"
#include "xb_stage_effects.h"
#include "dr_stage_collision.h"
#include "dr_container.h"
#include "dr_texfile.h"
#include <stdint.h>
#include <string.h>
#include <math.h>
#ifdef malloc
#undef malloc
#endif
#include <stdlib.h>
enum {BAD=-943,MEMORY=-944};
#define BIG (__BYTE_ORDER__==__ORDER_BIG_ENDIAN__)
static uint32_t rd(const void*p){return XbRes_Le32(p);}
static uint32_t native(const void*p){uint32_t v;memcpy(&v,p,4);return v;}
static float rf(const void*p){uint32_t v=rd(p);float f;memcpy(&f,&v,4);return f;}
static void words(void*d,const uint8_t*s,size_t n){for(size_t i=0;i<n;i+=4){uint32_t v=rd(s+i);memcpy((uint8_t*)d+i,&v,4);}}
static void param_halves(void*d,const uint8_t*s,size_t n){for(size_t i=0;i<n;i+=2){uint16_t v=s[i]|(unsigned)s[i+1]<<8;memcpy((uint8_t*)d+i,&v,2);}}
static int param_finite(const uint8_t*p,unsigned n){for(unsigned i=0;i<n;i++)if(!isfinite(rf(p+i*4)))return DR_STAGE_NUMBER;return 0;}
static int param_span(size_t n,uint32_t at,size_t count,size_t stride,size_t min){return at<min||(at&3)||at>n||count>(n-at)/stride;}
static int range(const DrStageCollision*v,uint32_t word,size_t n,size_t*at){uint64_t a=(uint64_t)word*4;if(a>v->size||n>v->size-a)return DR_STAGE_BOUNDS;*at=(size_t)a;return 0;}
typedef struct StageMeshWrite {DrStageCollision source;uint8_t*arena;uint32_t empty[8];} StageMeshWrite;
static int native_mesh(StageMeshWrite *c,uint32_t word,void **out){
 if(!word){*out=&c->empty;return 0;}
 const DrStageCollision *v=&c->source;size_t at;if(range(v,word,24,&at))return DR_STAGE_BOUNDS;
 const uint8_t *h=v->base+at;uint32_t n=rd(h);words(c->arena+at,h,24);*out=c->arena+at;
 if((int32_t)n<=0)return 0;
 size_t nodes;if(n>v->size/32 || range(v,rd(h+12),(size_t)n*32,&nodes))return DR_STAGE_BOUNDS;
 if(nodes&15)return DR_STAGE_BOUNDS;
 words(c->arena+nodes,v->base+nodes,(size_t)n*32);
 uint32_t polys=0;
 for(uint32_t i=0;i<n;i++){
  const uint8_t *node=v->base+nodes+(size_t)i*32;
  if(rd(node+28)==UINT32_MAX && rd(node+24)!=UINT32_MAX){uint32_t poly=rd(node+24);if(poly>=v->size/32)return DR_STAGE_BOUNDS;if(poly+1>polys)polys=poly+1;}
 }
 size_t p;if(range(v,rd(h+16),(size_t)polys*32,&p))return DR_STAGE_BOUNDS;
 if(p&15)return DR_STAGE_BOUNDS;
 words(c->arena+p,v->base+p,(size_t)polys*32);
 uint32_t vertices=0;
 for(uint32_t i=0;i<polys;i++)for(unsigned j=0;j<3;j++){
  uint32_t index=rd(v->base+p+(size_t)i*32+4+j*4);if(index>=v->size/16)return DR_STAGE_BOUNDS;if(index+1>vertices)vertices=index+1;
 }
 size_t a;if(range(v,rd(h+20),(size_t)vertices*16,&a))return DR_STAGE_BOUNDS;
 if(a&15)return DR_STAGE_BOUNDS;
 words(c->arena+a,v->base+a,(size_t)vertices*16);return 0;
}
typedef struct StageBuild {DrStageCollision input;uint8_t *dest,*seen;uint32_t visibility,texture_count;XbStagePackInfo info;} StageBuild;
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
 if(b->seen[at/4]&16)return DR_STAGE_TREE;
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
 StageMeshWrite converter={0};converter.source=b->input;converter.arena=b->dest;
 for(unsigned i=0;i<zones;i++){
  size_t zone=at+i*64;const uint8_t *p=h+zone;size_t near,recs;void *ignored;
  if(geom_span(b,rd(p+12),rd(p+8),4,&near) || geom_span(b,rd(p+24),rd(p+20),16,&recs))return DR_STAGE_BOUNDS;
  geom_words(b,zone,64);geom_words(b,near,(size_t)rd(p+8)*4);geom_words(b,recs,(size_t)rd(p+20)*16);
  e=native_mesh(&converter,rd(p+4),&ignored);if(e)return e;
  for(unsigned j=0;j<rd(p+20);j++){
   uint32_t mesh=rd(h+recs+j*16+8);e=native_mesh(&converter,mesh,&ignored);if(e)return e;
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
typedef struct ParamSchema {uint8_t stride;uint16_t words,negate;uint32_t halves;} ParamSchema;
static const ParamSchema param_schema[20]={
 {64,0xff8f,0x600,0},{16,15,4,0},{32,255,0x66,0},{16,15,6,0},
 {32,255,0x66,0},{32,255,0x66,0},{32,255,0x66,0},{16,1,0,0},
 {16,3,0,0},{16,1,0,0},{32,31,6,1u<<10},
 {24,31,6,(1u<<10)|(1u<<11)},{24,0x19,0,1u<<10},
 {4,1,0,0},{12,0,0,0},{8,0,0,0},{4,0,0,0},{8,1,0,0},
 {4,1,0,0},{24,0x21,0,0}
};
static int mapd(uint8_t*params,const uint8_t*p,size_t bytes,unsigned objects){
 size_t start[20],end[20];int e=BAD;
 if(bytes<168||bytes>0x100000||rd(p)!=0x4450414d||rd(p+4))return BAD;
 words(params,p,168);
 for(unsigned i=0;i<20;i++){
  uint32_t count=rd(p+8+i*8),off=rd(p+12+i*8);const ParamSchema *s=&param_schema[i];
  if(param_span(bytes,off,count,s->stride,168)){e=DR_STAGE_BOUNDS;return e?e:BAD;}
  start[i]=off;end[i]=off+(size_t)count*s->stride;
  for(unsigned j=0;j<i;j++)if(count && end[j]>start[j] && start[i]<end[j] && start[j]<end[i])return e?e:BAD;
  for(uint32_t j=0;j<count;j++){
   const uint8_t *src=p+off+(size_t)j*s->stride;uint8_t *dst=params+off+(size_t)j*s->stride;
   for(unsigned k=0;k<s->stride/4;k++)if(s->words&(1u<<k))words(dst+k*4,src+k*4,4);
   for(unsigned k=0;k<s->stride/2;k++)if(s->halves&(1u<<k))param_halves(dst+k*2,src+k*2,2);
   unsigned floats=i==2?8:i>=3&&i<=6?(i==3?4:8):i==10?5:i==11?4:0;
   if((floats && param_finite(src,floats)) || (i==0 && param_finite(src+32,8)) || (i==1 && param_finite(src+8,1)) || (i==12 && param_finite(src+12,2)) || (i==19 && param_finite(src+20,1))){e=DR_STAGE_NUMBER;return e?e:BAD;}
  }
 }
 /* MAPD also includes animation-only objects without collision objects. */
 if(rd(p+8)<objects || rd(p+16)!=1 || rd(p+24)!=1 || rd(p+32)!=1 || rd(p+40)!=2 || rd(p+48)!=1 || rd(p+56)!=1)return e?e:BAD;
 for(uint32_t i=objects;i<rd(p+8);i++){
  /* Original animation consumers may query collision state unless this bit
   * is set. Extra MAPD objects must be animation-only, as in the real packs. */
  const uint8_t *def=p+start[0]+(size_t)i*64;
  if(!(rd(def+4)&0x40000000u) || (int32_t)rd(def+8)<=0){e=DR_STAGE_FORMAT;return e?e:BAD;}
 }
 for(uint32_t i=0;i<rd(p+8);i++){
  int32_t parent=(int32_t)i;uint32_t budget=rd(p+8)+1;
  while(parent>=0){if((uint32_t)parent>=rd(p+8) || !budget--){e=DR_STAGE_TREE;return e?e:BAD;}parent=(int32_t)rd(p+start[0]+(size_t)parent*64+12);}
 }
 return 0;
}

/* Native relative metadata only. The ORIGINAL binders relocate and negate coordinates once. */
static int paths(uint8_t*d,const uint8_t*s,size_t n){
 if(n<32||rd(s)!=0x70505645||rd(s+4)!=1||rd(s+16))return BAD;
 unsigned count=rd(s+8),off=rd(s+12);if(count>256||param_span(n,off,count,16,32))return BAD;
 words(d,s,32);
 for(unsigned i=0;i<count;i++){
  const uint8_t*q=s+off+i*16;unsigned last=rd(q),point=rd(q+4),first=rd(q+8);
  if(first>=last||param_span(n,point,last,48,off+(size_t)count*16))return BAD;
  words(d+off+i*16,q,16);
  for(unsigned j=0;j<last;j++){if(param_finite(s+point+(size_t)j*48,3))return BAD;words(d+point+(size_t)j*48,s+point+(size_t)j*48,12);}
 }return 0;
}
static int nav(uint8_t*d,const uint8_t*s,size_t n,unsigned objects){
 if(n<16||rd(s)!=1)return BAD;
 unsigned count=rd(s+4);if(!count||count>256||count>(n-16)/48)return BAD;
 words(d,s,12);memset(d+12,0,4);
 for(unsigned i=0;i<count;i++){
  const uint8_t*q=s+16+(size_t)i*48;int obj=(int)rd(q+20);
  if(param_finite(q,4)||obj< -1||(obj>=0&&(unsigned)obj>=objects))return BAD;
  words(d+16+(size_t)i*48,q,32);param_halves(d+48+(size_t)i*48,q+32,16);
  for(unsigned k=0;k<8;k++){int16_t link=(int16_t)(q[32+k*2]|(unsigned)q[33+k*2]<<8);if(link< -1||link>=(int)count)return BAD;}
 }return 0;
}
static int mef(uint8_t*d,const uint8_t*s,size_t n){
 if(n<32||rd(s)!=0x3046454d)return BAD;
 words(d,s,32);
 for(unsigned i=0;i<3;i++){
  unsigned count=rd(s+8+i*8),off=rd(s+12+i*8);if(param_span(n,off,count,48,32))return BAD;
  for(unsigned j=0;j<count;j++){const uint8_t*q=s+off+(size_t)j*48;if(param_finite(q,3))return BAD;words(d+off+(size_t)j*48,q,12);}
 }return 0;
}
static int primary_texture(StageBuild*b){
 size_t at;if(geom_span(b,rd(b->input.base+84),1,32,&at))return BAD;
 uint8_t*t=malloc(b->input.size);if(!t)return MEMORY;
 memcpy(t,b->input.base,b->input.size);memcpy(t,b->input.base+at,32);
 unsigned count=rd(t),table=rd(t+4);int e=dr_texfile_prepare(t,b->input.size,BIG);
 if(!e){memcpy(b->dest+at,t,32);memcpy(b->dest+(size_t)table*4,t+(size_t)table*4,(size_t)count*64);b->texture_count=count;b->info.textures=count;}
 free(t);return e;
}
static int stage(uint8_t*d,const uint8_t*s,size_t n,XbStagePackInfo*info){
 DrStageCollision v;int e=dr_stage_collision_open(s,n,&v);if(e)return e;
 size_t first=(size_t)(v.base-s);StageBuild b={.input=v,.dest=d+first};
 b.seen=calloc(1,v.size/4+1);if(!b.seen)return MEMORY;
 e=primary_texture(&b);if(!e)e=geom_tables(&b);free(b.seen);if(e)return e;
 for(unsigned i=1;i<25;i++){
  size_t at,bytes;unsigned flags;e=dr_pack_memory_entry(s,n,i,&at,&bytes,&flags);if(e==DR_NOT_FOUND)continue;if(e||flags)return BAD;
  const uint8_t*p=s+at;uint8_t*out=d+at;
  if(i==1)e=mapd(out,p,bytes,v.objects);
  else if(i>=2&&i<=4)e=XbStageCamera_Prepare(out,p,bytes);
  else if(i==5||i==19||i==20){unsigned limit=i==19?4:32;if(rd(p)>limit)return BAD;e=dr_texfile_prepare(out,bytes,BIG);}
  else if(i==6||i==12||i==21||i==22)e=XbStageEffects_PrepareEntry(i,out,p,bytes);
  else if(i==15)e=nav(out,p,bytes,v.objects);
  else if(i==18)e=mef(out,p,bytes);
  else if(i==23)e=paths(out,p,bytes);
  else return BAD; /* Other stages/animation packs are not registered by this initial profile. */
  if(e)return e;
  b.info.entries++;
 }
 b.info.entries++;words(d,s,27*4);if(info)*info=b.info;return 0;
}
int XbStagePack_Prepare(void*buf,size_t n,XbStagePackInfo*info){
 if(info)memset(info,0,sizeof(*info));
 if(!buf||n<108||n>0x6cb800||rd(buf)!=25)return BAD;
 unsigned prev=108;for(unsigned i=0;i<=25;i++){unsigned at=rd((const uint8_t*)buf+4+i*4);if((at&3)||at<prev||at>n)return BAD;prev=at;}if(prev!=n)return BAD;
 uint8_t*d=malloc(n);if(!d)return MEMORY;memcpy(d,buf,n);XbStagePackInfo stats={0};int e=stage(d,buf,n,&stats);
 if(!e){memcpy(buf,d,n);if(info)*info=stats;}free(d);return e;
}
static int prepare(void*p,size_t n,int id){if(id!=0x171)return BAD;return XbStagePack_Prepare(p,n,NULL);}
int XbStagePack_RegisterInitial(void){return XbRes_RegisterChecked(0x171,0x171,prepare,0);}
void *XbStagePack_NullMesh(void*base){
 static uint32_t empty[8] __attribute__((aligned(16)));
 const void*file;size_t n;int id;
 if(XbRes_ReadFile(base,&file,&n,&id)&&id==0x171&&n>=108&&native(file)==25&&(const uint8_t*)base==(const uint8_t*)file+native((const uint8_t*)file+4))return empty;
 return base; /* Preserve the legacy path for buffers outside this converter. */
}
/* The old relocator rebases the texture-animation header, but not frames. */
void XbStagePack_RelocateTextureFrames(void*base){
#if UINTPTR_MAX==UINT32_MAX
 if(XbStagePack_NullMesh(base)==base)return;
 uint8_t*h=base;uint8_t*groups=(uint8_t*)(uintptr_t)native(h+28);
 for(unsigned g=0;g<native(h+24);g++){
  uint8_t*group=groups+g*8;uint16_t count;memcpy(&count,group+2,2);
  uint8_t*mats=(uint8_t*)(uintptr_t)native(group+4);
  for(unsigned i=0;i<count;i++){
   uint8_t*anim=(uint8_t*)(uintptr_t)native(mats+i*16+8);if(!anim)continue;
   uint32_t at=native(anim+4);
   const void*file;size_t n;int id;
   if(!XbRes_ReadFile(base,&file,&n,&id))return;
   if(at<n/4){uint32_t ptr=(uint32_t)(uintptr_t)base+at*4;memcpy(anim+4,&ptr,4);}
  }
 }
#else
 (void)base;
#endif
}
#ifndef XB_HOST_TEST
#include "sys/gfxm_e.h"
extern StgRelocHdr *gBtlStage;
extern void BtlStage_Relocate(s32 *);
/* Isolated ORIGINAL reader check, without BtlStage_Init, physics or rendering.
 * The caller discards the converted buffer after this consuming relocation. */
int XbStagePack_CheckRelocation(void*file,size_t n){
 const void*tracked;size_t length;int id;
 if(gBtlStage||!XbRes_ReadFile(file,&tracked,&length,&id)||tracked!=file||id!=0x171||length!=n)return BAD;
 uint8_t*base=(uint8_t*)file+native((uint8_t*)file+4);
 gBtlStage=(StgRelocHdr*)base;BtlStage_Relocate((s32*)base);
 int e=0;unsigned materials=0,meshes=0;
 if(gBtlStage->base!=(s32*)base||gBtlStage->aCount!=7||!gBtlStage->tex||gBtlStage->tex->count!=25)e=BAD;
 for(unsigned g=0;!e&&g<gBtlStage->aCount;g++){
  StgRelocA*group=gBtlStage->a+g;
  for(unsigned i=0;i<group->count;i++){
   StgRelocA1*mat=group->items+i;materials++;meshes+=mat->count;
   for(unsigned j=0;j<mat->count;j++){
    uintptr_t ptr=(uintptr_t)mat->items[j].ptr8;
    if(ptr<(uintptr_t)base||ptr-(uintptr_t)file>=n)e=BAD;
   }
  }
 }
 for(unsigned i=0;!e&&i<gBtlStage->dCount;i++){
  StgRelocD*zone=gBtlStage->d+i;
  if(!zone->ptr04)e=BAD;
  for(int j=0;j<zone->count;j++)if(!zone->items[j].ptr8)e=BAD;
 }
 if(materials!=34||meshes!=2243)e=BAD;
 gBtlStage=NULL;return e;
}
#endif
