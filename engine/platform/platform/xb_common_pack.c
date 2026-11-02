/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_common_pack.h"
#include "xb_battle_common.h"
#include "xb_stage_camera.h"
#include "xb_resconv.h"
#include "dr_texfile.h"
#include "dr_container.h"
#include <stdint.h>
#ifdef malloc
#undef malloc
#endif
#include <stdlib.h>
#include <string.h>
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define BIG 1
#else
#define BIG 0
#endif
static uint32_t common3_word(const uint8_t *p){return XbRes_Le32(p);}
#include "xb_common3_formats.inc"
static void half(uint8_t*d,const uint8_t*s,size_t at){uint16_t v=c3h(s+at);memcpy(d+at,&v,2);}
static void word(uint8_t*d,const uint8_t*s,size_t at){uint32_t v=c3u(s+at);memcpy(d+at,&v,4);}
static uint64_t hash(const uint8_t*s,size_t n){uint64_t h=UINT64_C(0xcbf29ce484222325);for(size_t i=0;i<n;i++)h=(h^s[i])*UINT64_C(0x100000001b3);return h;}
static int span(const uint8_t*s,size_t size,unsigned i,const uint8_t**p,size_t*n){
 unsigned count=c3u(s);if(i>=count)return -936;
 uint32_t a=c3u(s+4+i*4),z=c3u(s+8+i*4);if(a>z||z>size)return -936;
 *p=s+a;*n=z-a;return 0;
}
static int common2(uint8_t*d,const uint8_t*s,size_t size){
 const uint8_t*p;size_t n,w;int e;static const unsigned ids[]={2,4,5,6,7,8,3};
 if(span(s,size,0,&p,&n))return -936;
 if((e=dr_texfile_prepare(d+(p-s),n,BIG)))return e;
 for(unsigned i=0;i<7;i++){
  if(span(s,size,ids[i],&p,&n))return -936;
  if((e=XbBattleCommon_Convert(i,p,n,BIG,d+(p-s),n,&w)))return e;
 }
 for(unsigned i=0;i<213;i++){
  if(span(s,size,i,&p,&n))return -936;
  uint8_t*out=d+(p-s);
  if(i==1||i==9||i==10){if(n)return -936;}
  else if(i==11){if((e=XbStageCamera_Prepare(out,p,n)))return e;}
  else if(i==12){
   /* Only bound as opaque common900 in the pinned source, with no internal
    * reader. Preserve this exact USA variant; do not invent its schema. */
   if(n!=896||hash(p,n)!=UINT64_C(0xf3d0da9ecfd56a0b))return -936;
  }else if(i>=13){
   if(n<16||memcmp(p,"LIPS",4)||c3u(p+4)!=2||p[10]||p[11])return -936;
   unsigned count=c3h(p+8),prev=0;if(!count||count>(n-16)/4)return -936;
   for(unsigned j=0;j<count;j++){unsigned frame=c3h(p+16+j*4);if(j&&frame<=prev)return -936;prev=frame;}
   half(out,p,8);for(unsigned j=0;j<count*2;j++)half(out,p,16+j*2);
  }
 }
 return 0;
}
static int common5(uint8_t*d,const uint8_t*s,size_t size){
 const uint8_t*p;size_t n;int e;
 for(unsigned i=0;i<11;i++){
  if(span(s,size,i,&p,&n))return -936;
  uint8_t*out=d+(p-s);
  if(i<2){if((e=dr_font_prepare(out,n,BIG)))return e;}
  else if(i<7){if(n)return -936;}
  else if(i==7){if((e=dr_texfile_prepare(out,n,BIG)))return e;}
  else{if(n<4||n%2||c3h(p)!=0xfeff)return -936;int end=0;for(size_t at=2;at<n;at+=2)if(!c3h(p+at)){end=1;break;}if(!end)return -936;for(size_t at=0;at<n;at+=2)half(out,p,at);}
 }
 return 0;
}
static const unsigned c3sets[]={0xc,0x20,0x25,0x4c,0x66,0x73,0x88,0x91,0xa4,0xb9,0xf2,0x11a,0x137,0x158,0x192,0x1aa,0x1c0,0x1c1,0x1cb,0x1e6,0x1fc,0x217,0x23f,0x268};
static int c3entry(const void *pack,size_t n,unsigned index,const uint8_t **bytes,size_t *length){
 size_t off;unsigned flags;int e=dr_pack_memory_entry(pack,n,index-1,&off,length,&flags);
 if(e)return e;
 if(flags)return -890;
 *bytes=(const uint8_t *)pack+off;return 0;
}
static int common3(uint8_t*destination,const uint8_t*source,size_t n){
 const void*pack=source;unsigned count=637;
 uint8_t type[638]={0},kind[638]={0},second[638]={0};
 unsigned textures=0,images=0,prepared=0,diagnostic_id=0;int error=0;
 for(unsigned set=0;set<sizeof(c3sets)/sizeof(*c3sets);set++){
  unsigned id=c3sets[set];diagnostic_id=id;const uint8_t *s;size_t bytes;
  error=c3entry(pack,n,id,&s,&bytes);if(error)goto fail;
  if(bytes<64){error=-890;goto fail;}
  unsigned groups=s[4],parts=s[5],sum=0,res=0,pairs=0,next=id+1;uint32_t mask=0;
  if(groups>19 || parts>40 || ((64+32*groups+64*parts+63)&~63u)!=bytes){error=-890;goto fail;}
  type[id]=2;
  for(unsigned g=0;g<groups;g++){
   const uint8_t *q=s+64+g*32;unsigned k=q[0],np=q[1],nr=q[2];
   if(k>=19 || mask&(1u<<k) || sum+np>parts || (k && (!nr || res+nr>32 || pairs+np>40))){error=-890;goto fail;}
   mask|=1u<<k;sum+=np;if(!k)continue;
   res+=nr;pairs+=np;
   for(unsigned j=0;j<nr;j++){if(next>count || type[next]){error=-890;goto fail;}type[next++]=1;}
   for(unsigned j=0;j<np;j++)for(unsigned which=0;which<(k==2 || k==14?1u:2u);which++){
    if(next>count || type[next]){error=-890;goto fail;}
    type[next]=k==14?4:3;kind[next]=(uint8_t)k;second[next]=(uint8_t)which;next++;
   }
   unsigned first=sum-np;
   for(unsigned j=0;j<np;j++)if(s[64+groups*32+(first+j)*64+1]>=nr){error=-890;goto fail;}
  }
  if(mask!=c3u(s)){error=-890;goto fail;}
 }
 for(unsigned id=1;id<=count;id++){
  diagnostic_id=id;const uint8_t *s;size_t bytes;error=c3entry(pack,n,id,&s,&bytes);if(error)goto fail;
  if(!type[id]){
   if(id==0x239 || id==0x23b)type[id]=5;
   else if(id==1 || id==4 || id==7 || id==0x23d || id==0x23e || (id>=0x24e && id<=0x25b) || id==0x25d || id==0x25f || id==0x260 || (id>=0x261 && id<=0x267)){
    if(id!=0x259)type[id]=6;
   }
  }
  if(!type[id] && bytes>=32 && c3u(s+4)==8 && c3u(s)>0 && c3u(s)<=256)type[id]=1;
  if(type[id]==1){
   error=dr_texfile_prepare(destination+(s-source),bytes,BIG);if(error)goto fail;
   textures++;images+=c3u(s);prepared++;continue;
  }
  uint8_t *d=destination+(s-source);
  if(type[id]==2){
   error=c3fields(d,s,bytes,0,4,4,0);
   if(!error)error=c3fields(d,s,bytes,8,32,4,1);
   if(!error)error=c3fields(d,s,bytes,40,64,4,1);
   for(unsigned j=0;!error && j<s[5];j++)error=c3fields(d,s,bytes,64+s[4]*32+j*64+16,64+s[4]*32+j*64+52,4,1);
  }else if(type[id]==3 || type[id]==4)error=c3param(d,s,bytes,kind[id],second[id]);
  else {error=c3standalone(d,s,bytes,id);if(!error)type[id]=(id==0x239 || id==0x23b)?5:6;}
  if(error)goto fail;
  prepared++;
 }

 (void)textures;(void)images;(void)prepared;(void)diagnostic_id;return 0;
fail:return error;
}
int XbCommonPack_Prepare(void*buf,size_t n,int id){
 unsigned count=id==2?213:id==3?637:id==5?11:0;
 if(!buf||!count||n>16u*1024*1024||n<(count+2)*4||c3u(buf)!=count)return -936;
 const uint8_t*s=buf;uint32_t prev=(count+2)*4;
 for(unsigned i=0;i<=count;i++){uint32_t at=c3u(s+4+i*4);if((at&3)||at<prev||at>n)return -936;prev=at;}if(prev!=n)return -936;
 uint8_t*d=malloc(n);if(!d)return -937;memcpy(d,s,n);
 int e=id==2?common2(d,s,n):id==3?common3(d,s,n):common5(d,s,n);
 if(!e){for(unsigned i=0;i<count+2;i++)word(d,s,i*4);memcpy(buf,d,n);}free(d);return e;
}
static int prepare(void*p,size_t n,int id){return XbCommonPack_Prepare(p,n,id);}
int XbCommonPack_Register(void){static const XbResCheckedRange ranges[]={{2,3,prepare,0},{5,5,prepare,0}};return XbRes_RegisterCheckedBatch(ranges,2);}
