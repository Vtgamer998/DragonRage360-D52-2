/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_model_pack.h"
#include "xb_resconv.h"
#include "xb_fighter_tables.h"
#include "xb_stage_camera.h"
#include "dr_body_model.h"
#include "dr_resource.h"
#include "dr_face_vif.h"
#include "dr_texfile.h"
#include <stdint.h>
#ifdef malloc
#undef malloc
#endif
#include <stdlib.h>
#include <string.h>
#include <math.h>
enum {BAD=-941,MEMORY=-942};
static uint32_t le(const void *p){return XbRes_Le32(p);}
static unsigned half(const uint8_t*p){return p[0]|(unsigned)p[1]<<8;}
static uint32_t native(const void*p){uint32_t v;memcpy(&v,p,4);return v;}
static int zero(const uint8_t*p,size_t n){for(size_t i=0;i<n;i++)if(p[i])return 0;return 1;}
static uint64_t hash(const uint8_t*p,size_t n){uint64_t h=UINT64_C(0xcbf29ce484222325);for(size_t i=0;i<n;i++)h=(h^p[i])*UINT64_C(0x100000001b3);return h;}
static void flip(uint8_t*p,size_t at,unsigned w,int be){if(be)for(unsigned i=0;i<w/2;i++){uint8_t v=p[at+i];p[at+i]=p[at+w-1-i];p[at+w-1-i]=v;}}
static int fields(uint8_t*d,const uint8_t*s,size_t n,size_t a,size_t z,unsigned w,int be,int floating){
 if(a>z||z>n||(z-a)%w)return BAD;
 for(size_t i=a;i<z;i+=w){if(floating){uint32_t v=le(s+i);float f;memcpy(&f,&v,4);if(!isfinite(f))return BAD;}flip(d,i,w,be);}return 0;
}
static int table(const uint8_t*s,size_t n){
 if(!s||n<254*4||n>0x240000||le(s)!=252)return BAD;
 uint32_t prev=254*4;for(unsigned i=0;i<=252;i++){uint32_t at=le(s+4+i*4);if((at&3)||at<prev||at>n)return BAD;prev=at;}return prev==n?0:BAD;
}
static void entry(const uint8_t*s,unsigned i,const uint8_t**p,size_t*n){uint32_t a=le(s+4+i*4);*p=s+a;*n=le(s+8+i*4)-a;}
static void put(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
typedef struct Rebase {uint8_t*chain;uint32_t tbp,cbp;unsigned phase;} Rebase;
static int visit(void*ctx,const DrFaceVifCommand*c){
 Rebase*r=ctx;if(c->code!=0x6c058000)return 0;if(c->data_bytes<40)return BAD;
 uint8_t*p=r->chain+c->code_offset;uint32_t a=le(p+20),b=le(p+24),d=le(p+36),e=le(p+40),t=a&0x3fff,u=(b>>5)&0x3fff;
 if(!r->phase){if(t<r->tbp)r->tbp=t;if(u<r->cbp)r->cbp=u;return 0;}
 if(t<r->tbp||u<r->cbp)return BAD;
 t=t-r->tbp+0x3480;u=u-r->cbp+0x3c00;uint32_t v=((e>>5)&0x3fff)+0x3d00;
 if(t>0x3fff||u>0x3fff||v>0x3fff)return BAD;
 if(r->phase==2){put(p+20,(a&~0x3fffu)|t);put(p+24,(b&0xfff8001fu)|(u<<5));put(p+36,(d&~0x3fffu)|0x3d40);put(p+40,(e&0xfff8001fu)|(v<<5));}return 0;
}
static int chain(Rebase*r,size_t n){size_t bad;return dr_face_vif_parse(r->chain,n,visit,r,NULL,&bad);}
static int geometry(uint8_t*d,const uint8_t*s,size_t n,int be){
 DrBodyPart parts[256];DrBodyModel model;size_t bad;int e=dr_body_model_read(s,n,parts,256,&model,&bad);if(e)return e;
 uint8_t seen[162]={0};for(unsigned i=0;i<model.parts;i++){if(parts[i].node>=162||seen[parts[i].node]++)return BAD;}
 Rebase r={.tbp=0x7fffffff,.cbp=0x7fffffff};
 for(r.phase=0;r.phase<3;r.phase++)for(unsigned i=0;i<model.parts;i++)if(parts[i].enabled&&parts[i].chain_bytes){r.chain=d+parts[i].chain_offset;if((e=chain(&r,parts[i].chain_bytes)))return e;}
 if((e=fields(d,s,n,12,16,4,be,0)) || (e=fields(d,s,n,24,44,4,be,0)) || (e=fields(d,s,n,88,112,4,be,0)))return e;
 for(unsigned i=0;i<model.parts;i++){
  size_t a=parts[i].record_offset;
  if((e=fields(d,s,n,a,a+4,4,be,0)) || (e=fields(d,s,n,a+4,a+12,2,be,0)) || (e=fields(d,s,n,a+12,a+64,4,be,0)))return e;
  if(parts[i].record_bytes>64 && (e=fields(d,s,n,a+64,a+96,4,be,0)))return e;
 }return 0;
}
static int body(uint8_t*d,const uint8_t*s,size_t n,int be){
 if(n<480)return BAD;
 int e=fields(d,s,n,0,8,4,be,0);if(e)return e;
 for(size_t at=16;;at+=0x1d0){
  if(at>n||n-at<0x1d0||le(s+at+4)>=162||!zero(s+at+8,8))return BAD;
  if((e=fields(d,s,n,at,at+8,4,be,0)) || (e=fields(d,s,n,at+16,at+48,4,be,1)) || (e=fields(d,s,n,at+64,at+0x1d0,4,be,1)))return e;
  if(le(s+at)&1)return zero(s+at+0x1d0,n-at-0x1d0)?0:BAD;
 }
}
static int lips(uint8_t*d,const uint8_t*s,size_t n,int be){
 if(n<16||memcmp(s,"LIPS",4)||le(s+4)!=2||s[10]||s[11])return BAD;
 unsigned count=half(s+8),prev=0;
 if(!count||count>(n-16)/4)return BAD;
 for(unsigned i=0;i<count;i++){unsigned frame=half(s+16+i*4);if(i&&frame<=prev)return BAD;prev=frame;}
 flip(d,8,2,be);return fields(d,s,n,16,16+count*4,2,be,0);
}
static int convert(uint8_t*d,const uint8_t*s,size_t size,int be){
 static const unsigned tables[]={17,18,19,20,23,24,26,27};
 const uint8_t*p;size_t n,w;int e;
 for(unsigned i=0;i<252;i++){
  entry(s,i,&p,&n);uint8_t*out=d+(p-s);if(!n)continue;
  if(i==0||(i>=11&&i<=16)){if((e=dr_texfile_prepare(out,n,be)))return e;}
  else if(i==1){if((e=body(out,p,n,be)))return e;}
  else if(i==2){if((e=geometry(out,p,n,be)))return e;}
  else if(i>=3&&i<=10){Rebase r={.chain=out,.tbp=0,.cbp=0};for(r.phase=1;r.phase<3;r.phase++)if((e=chain(&r,n)))return e;}
  else if(i==21){uint64_t h=hash(p,n);if(n!=704||(h!=UINT64_C(0xc44088b4d529ce4e)&&h!=UINT64_C(0xe537ce97b8a967e0)))return BAD;}
  else if(i==22){
   if(n<0x350)return BAD;
   static const unsigned regions[][3]={{0x10,0xd0,4},{0xe0,0x100,2},{0x110,0x190,4},{0x1a0,0x1c0,2},{0x1d0,0x250,4},{0x260,0x2e0,4},{0x300,0x340,4}};
   for(unsigned j=0;j<7;j++)if((e=fields(out,p,n,regions[j][0],regions[j][1],regions[j][2],be,0)))return e;
  }else if(i>=28&&i<=35){if((e=dr_anim_prepare(out,n,be)))return e;}
  else if(i>=40&&i<=42){
   if((e=XbStageCamera_PrepareEndian(out,p,n,be)))return e;
  }else if(i>=43&&i<=51){if(n%2||half(p)!=0xfeff)return BAD;if((e=fields(out,p,n,0,n,2,be,0)))return e;}
  else if(i>=52){if((e=lips(out,p,n,be)))return e;}
  else {
   unsigned kind=0;while(kind<8&&tables[kind]!=i)kind++;if(kind==8)return BAD;
   if((e=XbFighterTable_Convert(kind,p,n,be,out,n,&w)))return e;
   if(w<n&&!zero(p+w,n-w)){
    uint64_t h=hash(p+w,n-w);if(i!=18||(h!=UINT64_C(0xaa1287a8ec918251)&&h!=UINT64_C(0xb77c0df4789a10d2)))return BAD;
   }
  }
 }
 return fields(d,s,size,0,254*4,4,be,0);
}
int XbModelPack_Prepare(void*buf,size_t n,int be){
 if((be!=0&&be!=1)||table(buf,n))return BAD;
 uint8_t*out=malloc(n);if(!out)return MEMORY;memcpy(out,buf,n);int e=convert(out,buf,n,be);if(!e)memcpy(buf,out,n);free(out);return e;
}
static int prepare(void*p,size_t n,int id){if(id!=0x590&&id!=0x59a)return BAD;return XbModelPack_Prepare(p,n,__BYTE_ORDER__==__ORDER_BIG_ENDIAN__);}
int XbModelPack_RegisterInitial(void){static const XbResCheckedRange ranges[]={{0x590,0x590,prepare,0},{0x59a,0x59a,prepare,0}};return XbRes_RegisterCheckedBatch(ranges,2);}
int XbModelPack_ChainPrepared(int single,const void*chainptr,int tbp,int cbp,int tbp2,int cbp2,unsigned min_tbp,unsigned min_cbp){
 const void*base=NULL;size_t n=0;int id=0;
 if(!XbRes_ReadFile(chainptr,&base,&n,&id)||(id!=0x590&&id!=0x59a))return 0;
 const uint8_t*p=base;uintptr_t at=(uintptr_t)chainptr-(uintptr_t)base;
 if(native(p)!=252||n<254*4||tbp!=0x3480||cbp!=0x3c00||tbp2!=0x3d40||cbp2!=0x3d00)goto bad;
 if(!single){size_t a=native(p+12);if(a<=n&&n-a>=112&&at==a+native(p+a+0x6c)&&min_tbp==0x7fffffff&&min_cbp==0x7fffffff)return 1;}
 else for(unsigned i=3;i<=10;i++)if(at==native(p+4+i*4)&&native(p+8+i*4)>at&&!min_tbp&&!min_cbp)return 1;
bad:XbRes_SetReadError(BAD);return 1;
}
