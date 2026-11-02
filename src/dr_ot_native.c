/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Packet field evidence: pinned battle/eft_aa.h, eft_ac.h, eft_q.h, eft_t.h,
 * eft_v.h, eft_y.h and local EftGfx* in eft_a.c. REGLIST RGBAQ is four byte
 * channels followed by f32 Q; ST is two f32; XYZF is x16/y16/z24/f8 bitfields.
 * Numeric u64 GIF tags/PRIM/TEX0 and PACKED A+D are a different representation.
 */
#include "dr_ot_native.h"
#include <stdint.h>
#include <string.h>
static uint64_t get(const uint8_t*p,unsigned n,int big){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<((big?n-1-i:i)*8);return v;}
static void put(uint8_t*p,uint64_t v,int big){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>((big?7-i:i)*8));}
static int pass(uint8_t *d,const uint8_t*s,size_t bytes,int big){
 for(size_t at=0;at<bytes;){
  if(bytes-at<16)return -420;
  uint64_t tag=get(s+at,8,big),regs=get(s+at+8,8,big);unsigned loops=tag&0x7fff,flg=(tag>>58)&3,nreg=tag>>60;if(!nreg)nreg=16;
  size_t count=(size_t)loops*nreg,payload;if(flg>1)return -420;
  payload=flg?((count+1)/2)*16:count*16;
  if(payload>bytes-at-16)return -420;
  for(unsigned j=0;j<nreg && count;j++){unsigned reg=(unsigned)(regs>>(4*j))&15;
   if(flg==0){if(reg!=14&&reg!=15)return -420;}
   else if(reg!=0&&reg!=1&&reg!=2&&reg!=4&&reg!=6&&reg!=7&&reg!=8&&reg!=9&&reg!=15)return -420;
  }
  if(d && big && flg==1)for(size_t i=0;i<count;i++){
   unsigned reg=(unsigned)(regs>>(4*(i%nreg)))&15;const uint8_t*p=s+at+16+i*8;uint64_t v=get(p,8,big);
   if(reg==1){uint32_t rgba=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;v=rgba|(get(p+4,4,big)<<32);}
   else if(reg==2)v=get(p,4,big)|(get(p+4,4,big)<<32);
   else if(reg==4)v=(v>>48)|((v>>32&0xffff)<<16)|((v>>8&0xffffff)<<32)|((v&255)<<56);
   put(d+at+16+i*8,v,big);
  }
  at+=16+payload;
 }
 return 0;
}
int dr_ot_payload(void *dst,const void *src,size_t bytes,int big){
 if((big!=0&&big!=1)||(!src&&bytes)||bytes>65536||bytes%16)return -420;
 if(!bytes)return 0;
 if(dst){uintptr_t a=(uintptr_t)dst,b=(uintptr_t)src;if(a<=b?b-a<bytes:a-b<bytes)return -420;}
 int e=pass(NULL,src,bytes,big);if(e || !dst)return e;
 memcpy(dst,src,bytes);return pass(dst,src,bytes,big);
}
