/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_stage_camera.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
static void put(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static void fixture(unsigned char *s){
 memset(s,0,240);put(s,0x6e414d43);put(s+4,1);put(s+8,80);put(s+12,2);put(s+20,0x41200000);put(s+24,36);put(s+32,1);
 put(s+80,0);put(s+84,112);put(s+88,2);put(s+96,1);put(s+100,176);put(s+104,2);
 put(s+120,0xc1200000);put(s+148,0x41200000);put(s+152,0x41a00000);
 put(s+184,0x3f800000);put(s+212,0x41200000);put(s+216,0x40000000);
}
int main(void){
 unsigned char src[240],out[240],bad[240];fixture(src);assert(!XbStageCamera_Prepare(out,src,sizeof(src)));
 uint32_t value;memcpy(&value,out+84,4);assert(value==112);float f;memcpy(&f,out+120,4);assert(f==-10.0f);assert(!memcmp(out+124,src+124,20));
 const unsigned positions[]={8,12,84,88,120,148,20,96};
 const uint32_t values[]={UINT32_MAX,9,232,UINT32_MAX,0x7fc00000,0xbf800000,0x7f800000,0};
 for(unsigned i=0;i<8;i++){
  memcpy(bad,src,sizeof(bad));put(bad+positions[i],values[i]);memset(out,0xa5,sizeof(out));
  assert(XbStageCamera_Prepare(out,bad,sizeof(bad))<0);
  for(unsigned k=0;k<sizeof(out);k++)assert(out[k]==0xa5);
 }
 assert(XbStageCamera_Prepare(out,src,35)<0);assert(XbStageCamera_Prepare(src,src,240)<0);
 puts("PASS stage camera: native scalars, opaque key payload, 8 malformed inputs rejected atomically");return 0;
}
