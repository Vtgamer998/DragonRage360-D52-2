/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_battle_common.h"
#include "dr_container.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
/* Reader evidence, upstream 3ca419c:
 * btl_char_status_anim: 414 u32 motion flags;
 * btl_char_cam: ChrCamCutDef; btl_char_get: BtlVoiceEntry;
 * btl_char_action: BtlActAttackRec; btl_char_member: BtlMemberParam;
 * btl_tech_b: BtlTechBPromptRow (bytes);
 * btl_ai_cond/seq: AiThData, BtlAiSeqBody/Step, AiThRuleList/Rule.
 * Profiles are byte rates, including their marker. The documentary
 * AiThProfile size 0x584 is NOT used: real sections are 0x580 bytes and
 * all scalar rate reads in this pinned source end by +0x57c. */
enum {FORMAT=-880,BOUNDS=-881,STATE=-882,MEMORY=-851};
struct XbBattleCommon {void *roster[6],*ai;size_t bytes[6],ai_bytes;XbBattleCommonInfo info;void *items;size_t items_bytes;unsigned char_count,item_count;};
static uint32_t r32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned r16(const uint8_t *p){return (unsigned)p[0]|(unsigned)p[1]<<8;}
static int zero(const uint8_t *p,size_t n){for(size_t i=0;i<n;i++)if(p[i])return 0;return 1;}
static void flip(uint8_t *p,size_t a,unsigned w,int be){if(be)for(unsigned i=0;i<w/2;i++){uint8_t x=p[a+i];p[a+i]=p[a+w-1-i];p[a+w-1-i]=x;}}
static void fields(uint8_t *p,size_t a,size_t z,unsigned w,int be){for(size_t i=a;i<z;i+=w)flip(p,i,w,be);}
static int ai_layout(const uint8_t *p,size_t n,XbBattleCommonInfo *info){
 if(n<0x148 || !zero(p+0xa4,0xa4))return FORMAT;
 size_t at=0x148;unsigned rules=0,profiles=0,steps=0;
 for(unsigned i=0;i<41;i++){
  uint32_t size=r32(p+i*4);if(size>n-at)return BOUNDS;
  const uint8_t *q=p+at;
  if(i==0){
   if(size<0x788 || (size-0x788)%20 || !zero(q+4,4))return FORMAT;
   steps=(size-0x788)/20;
   for(unsigned a=0;a<128;a++){
    unsigned first=r16(q+8+a*2),count=q[0x108+a];
    if(first>steps || count>steps-first || (count && q[0x188+a]>=16))return BOUNDS;
   }
   for(unsigned j=0;j<steps;j++)if(q[0x788+j*20]>=24)return FORMAT;
  }else if(i<9){
   if(size<8 || !zero(q+4,4))return FORMAT;
   unsigned count=r16(q);if(count>(size-8)/28 || !zero(q+8+count*28,size-8-count*28))return BOUNDS;
   for(unsigned j=0;j<count;j++){
    const uint8_t *r=q+8+j*28;if(r16(r)>=64)return FORMAT;
    for(unsigned k=0;k<8;k++){
     unsigned cond=r[4+k];if(cond==255)break;
     if(cond>=128 && cond!=254)return FORMAT;
    }
    if(r[20]==4 && (r[24]<127 || r[24]>132))return FORMAT;
   }
   rules+=count;
  }else if(size){if(size!=0x580)return FORMAT;profiles++;}
  at+=size;
 }
 if(!zero(p+at,n-at))return FORMAT;
 if(info){info->ai_steps=steps;info->ai_rules=rules;info->ai_profiles=profiles;}return 0;
}
int XbBattleCommon_Convert(unsigned kind,const void *src,size_t n,int be,void *dst,size_t cap,size_t *written){
 static const size_t strides[]={4,36,8,24,16,20};
 if(written)*written=0;
 if(kind>6 || !src || !dst || !written || (be!=0 && be!=1))return FORMAT;
 if(!n || n>128*1024 || cap<n)return BOUNDS;
 uintptr_t a=(uintptr_t)src,b=(uintptr_t)dst;if((a<=b && b-a<n) || (b<a && a-b<n))return STATE;
 const uint8_t *s=src;
 size_t count=0;
 if(kind==6){int e=ai_layout(s,n,NULL);if(e)return e;}
 else {count=n/strides[kind];if(!count || !zero(s+count*strides[kind],n-count*strides[kind]))return FORMAT;}
 uint8_t *p=dst;memcpy(p,s,n);
 if(kind==0 || kind==4)fields(p,0,count*strides[kind],4,be);
 else if(kind==1)for(size_t i=0;i<count;i++){size_t x=i*36;fields(p,x+4,x+32,4,be);fields(p,x+32,x+36,2,be);}
 else if(kind==2)for(size_t i=0;i<count;i++){size_t x=i*8;fields(p,x,x+4,2,be);flip(p,x+4,4,be);}
 else if(kind==3)for(size_t i=0;i<count;i++){size_t x=i*24;flip(p,x+6,2,be);flip(p,x+8,4,be);flip(p,x+20,2,be);}
 else if(kind==6){
  fields(p,0,0xa4,4,be);size_t at=0x148;
  for(unsigned i=0;i<41;i++){
   uint32_t size=r32(s+i*4);
   if(!i){
    fields(p,at,at+4,2,be);fields(p,at+8,at+0x108,2,be);fields(p,at+0x288,at+0x388,2,be);
    for(size_t x=at+0x788;x<at+size;x+=20){fields(p,x+4,x+8,2,be);fields(p,x+8,x+20,4,be);}
   }else if(i<9){flip(p,at,2,be);for(unsigned j=0;j<r16(s+at);j++)flip(p,at+8+j*28,2,be);}
   at+=size;
  }
 }
 *written=n;return 0;
}
void XbBattleCommon_Free(XbBattleCommon *c){if(c){for(unsigned i=0;i<6;i++)free(c->roster[i]);free(c->ai);free(c->items);free(c);}}
int XbBattleCommon_Prepare(const void *pack,size_t n,XbBattleCommon **out,XbBattleCommonInfo *info){
 static const unsigned entry[]={2,4,5,6,7,8,3};static const unsigned stride[]={4,36,8,24,16,20};
 if(!out || !info)return FORMAT;
 *out=NULL;memset(info,0,sizeof(*info));
 if(!pack || n<4 || r32(pack)!=213)return FORMAT;
 XbBattleCommon *c=calloc(1,sizeof(*c));if(!c)return MEMORY;int e=0;
 for(unsigned i=0;i<7;i++){
  size_t at,size,w;unsigned flags;e=dr_pack_memory_entry(pack,n,entry[i],&at,&size,&flags);if(e || flags){e=FORMAT;goto fail;}
  void *p=malloc(size);if(!p){e=MEMORY;goto fail;}if(i==6){c->ai=p;c->ai_bytes=size;}else{c->roster[i]=p;c->bytes[i]=size;c->info.roster_records[i]=(unsigned)(size/stride[i]);}
  e=XbBattleCommon_Convert(i,(const uint8_t *)pack+at,size,__BYTE_ORDER__==__ORDER_BIG_ENDIAN__,p,size,&w);if(e)goto fail;
  if(i==6){e=ai_layout((const uint8_t *)pack+at,size,&c->info);if(e)goto fail;}
 }
 if(c->info.roster_records[0]<414 || c->info.roster_records[2]<57 || c->info.roster_records[3]<62 || c->info.roster_records[4]<161){e=BOUNDS;goto fail;}
#if UINTPTR_MAX==UINT32_MAX
 /* These slots are pointer fields, not endian-swapped MIPS addresses. */
 uint8_t *p=c->ai;uint32_t sizes[41];memcpy(sizes,p,sizeof(sizes));size_t at=0x148;
 void *ptr=p+at;memcpy(p+0xa4,&ptr,4);ptr=p+0x8d0;memcpy(p+at+4,&ptr,4);at+=sizes[0];
 for(unsigned i=0;i<8;i++){ptr=p+at;memcpy(p+0xa8+i*4,&ptr,4);ptr=p+at+8;memcpy(p+at+4,&ptr,4);at+=sizes[1+i];}
 for(unsigned i=0;i<32;i++){ptr=sizes[9+i]?p+at:NULL;memcpy(p+0xc8+i*4,&ptr,4);at+=sizes[9+i];}
#endif
 *info=c->info;*out=c;return 0;
fail:XbBattleCommon_Free(c);return e;
}
int XbBattleCommon_GetRosterTable(XbBattleCommon *c,unsigned i,const void **p,size_t *n){
 if(!p || !n)return FORMAT;
 *p=NULL;*n=0;if(!c || i>=6)return FORMAT;*p=c->roster[i];*n=c->bytes[i];return 0;
}
int XbBattleCommon_GetAiData(XbBattleCommon *c,void **p,size_t *n){
 if(!p || !n)return FORMAT;
 *p=NULL;*n=0;if(!c || sizeof(void *)!=4)return STATE;*p=c->ai;*n=c->ai_bytes;return 0;
}

/* Typed common file 4, layouts in battle/view_b.h. Byte arrays stay bytes. */
static int item_layout(const uint8_t *p,size_t n,unsigned *nc,unsigned *ni){
 if(n<64 || n>128*1024 || r32(p)!=2)return FORMAT;
 size_t a=r32(p+4)&~3u,b=r32(p+8)&~3u,end=r32(p+12)&~3u;
 if(a<64 || b<a || end<b || end!=n || (b-a)/60!=161 || (end-b)/40<1)return BOUNDS;
 size_t chars=(b-a)/60,items=(end-b)/40;
 if(!zero(p+a+chars*60,b-a-chars*60) || !zero(p+b+items*40,end-b-items*40))return FORMAT;
 for(size_t i=0;i<chars;i++){
  const uint8_t *q=p+a+i*60;
  if(r32(q)>=32 || !r16(q+10) || r16(q+10)>4)return FORMAT;
  for(unsigned j=0;j<4;j++)if(q[44+j]!=255 && q[44+j]>=161)return BOUNDS;
  for(unsigned j=48;j<56;j+=4)if((r32(q+j)&0x7f800000)==0x7f800000)return FORMAT;
 }
 for(size_t i=0;i<items;i++)if(p[b+i*40]>3)return FORMAT;
 *nc=(unsigned)chars;*ni=(unsigned)items;return 0;
}
/* All validation precedes the first write, including the aliasing API. */
static void item_encode(uint8_t *p,const uint8_t *s,unsigned nc,unsigned ni,int be){
 size_t at=r32(s+4)&~3u,items=r32(s+8)&~3u;
 fields(p,0,16,4,be);
 for(unsigned i=0;i<nc;i++,at+=60){fields(p,at,at+8,4,be);fields(p,at+8,at+16,2,be);fields(p,at+16,at+44,4,be);fields(p,at+48,at+56,4,be);}
 at=items;
 for(unsigned i=0;i<ni;i++,at+=40){fields(p,at+12,at+20,2,be);fields(p,at+20,at+40,4,be);}
}
int XbBattleCommon_PrepareItems(void *p,size_t n,int be){
 unsigned nc,ni;int e;
 if(!p || (be!=0 && be!=1))return FORMAT;
 e=item_layout(p,n,&nc,&ni);if(e)return e;
 item_encode(p,p,nc,ni,be);return 0;
}
int XbBattleCommon_ConvertItems(const void *src,size_t n,int be,void *dst,size_t cap){
 unsigned nc,ni;int e;
 if(!src || !dst || (be!=0 && be!=1))return FORMAT;
 if(cap<n)return BOUNDS;
 uintptr_t a=(uintptr_t)src,b=(uintptr_t)dst;
 if((a<=b && b-a<n) || (b<a && a-b<n))return STATE;
 e=item_layout(src,n,&nc,&ni);if(e)return e;
 memcpy(dst,src,n);item_encode(dst,src,nc,ni,be);return 0;
}
int XbBattleCommon_AttachItems(XbBattleCommon *c,const void *src,size_t n){
 if(!c || !src || c->items)return STATE;
 unsigned nc,ni;int e=item_layout(src,n,&nc,&ni);if(e)return e;
 void *p=malloc(n);if(!p)return MEMORY;
 e=XbBattleCommon_ConvertItems(src,n,__BYTE_ORDER__==__ORDER_BIG_ENDIAN__,p,n);
 if(e){free(p);return e;}
 c->items=p;c->items_bytes=n;c->char_count=nc;c->item_count=ni;return 0;
}
int XbBattleCommon_GetItems(XbBattleCommon *c,void **p,size_t *n,unsigned *nc,unsigned *ni){
 if(!p || !n || !nc || !ni)return FORMAT;
 *p=NULL;*n=0;*nc=*ni=0;if(!c || !c->items)return STATE;
 *p=c->items;*n=c->items_bytes;*nc=c->char_count;*ni=c->item_count;return 0;
}
