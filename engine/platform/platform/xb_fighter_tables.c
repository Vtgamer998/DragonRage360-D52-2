/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_fighter_tables.h"
#include <stdint.h>
#include <string.h>
/* Layout evidence: btl_tech_a.h BtlParam/BtlAtkRecord, btl_tech_b.h
 * BtlTechBParam/BtlKiBlastData/BtlMoveData and btl_capi_a.h BtlCapiParam,
 * pinned upstream 3ca419c. No opaque arrays are presumed to be word arrays. */
static void flip(uint8_t *p,size_t off,unsigned width,int be){
 if(be)for(unsigned j=0;j<width/2;j++){uint8_t t=p[off+j];p[off+j]=p[off+width-1-j];p[off+width-1-j]=t;}
}
static void range(uint8_t *p,unsigned a,unsigned z,unsigned w,int be){for(unsigned i=a;i<z;i+=w)flip(p,i,w,be);}
int XbFighterTable_Convert(unsigned kind,const void *src,size_t bytes,int be,void *dst,size_t cap,size_t *written){
 static const size_t sizes[]={256,163*48,13*52,160,0x244,0xc8,0x200,0x24*10};
 if(written)*written=0;
 if(kind>=XB_FIGHTER_TABLES || !src || !dst || !written || (be!=0 && be!=1))return -860;
 size_t n=sizes[kind];if(bytes<n || cap<n)return -861;
 const uint8_t *in=src;
 /* No typed reader proves the parameter tail CC..FF: only verified-zero
  * USA variants are accepted. Reject other variants before touching output. */
 if(kind==XB_FIGHTER_PARAM)for(unsigned i=0xcc;i<256;i++)if(in[i])return -862;
 uintptr_t a=(uintptr_t)src,b=(uintptr_t)dst;
 if((a<=b && b-a<bytes) || (b<a && a-b<n))return -863;
 uint8_t *p=dst;memcpy(p,src,n);
 switch(kind){
 case XB_FIGHTER_PARAM:
  flip(p,0,2,be);range(p,4,0x58,4,be);range(p,0x5c,0x70,4,be);
  range(p,0x70,0x74,2,be);range(p,0x74,0x80,4,be);range(p,0x80,0x84,2,be);flip(p,0xc8,4,be);break;
 case XB_FIGHTER_ATTACK:
  for(unsigned i=0;i<163;i++){flip(p,i*48,4,be);range(p,i*48+4,i*48+0x12,2,be);}break;
 case XB_FIGHTER_KIBLAST:
  for(unsigned i=0;i<13;i++){flip(p,i*52,2,be);range(p,i*52+4,i*52+0x24,4,be);range(p,i*52+0x28,i*52+0x30,4,be);}break;
 case XB_FIGHTER_MOVE:range(p,0,160,4,be);break;
 case XB_FIGHTER_SUPER:
  range(p,0,0x18,4,be);range(p,0x18,0x24,2,be);range(p,0x24,0x30,4,be);
  range(p,0x54,0x78,4,be);range(p,0x16c,0x208,4,be);
  range(p,0x208,0x214,2,be);range(p,0x22c,0x244,4,be);break;
 case XB_FIGHTER_SKILL:
  range(p,0,0x10,4,be);range(p,0x10,0x18,2,be);range(p,0x18,0x38,4,be);
  range(p,0x74,0x94,4,be);range(p,0xa0,0xc8,4,be);break;
 /* All reachable character AI parameters are byte tables (btl_ai_cond/act).
  * In particular the FFFEFDFC marker is not a native integer. */
 case XB_FIGHTER_AI:break;
 case XB_FIGHTER_CAMERA:
  for(unsigned i=0;i<10;i++){range(p,i*0x24+4,i*0x24+0x20,4,be);range(p,i*0x24+0x20,i*0x24+0x24,2,be);}break;
 }
 *written=n;return 0;
}
