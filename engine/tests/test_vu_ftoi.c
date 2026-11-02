/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "dr_vu1.h"
#include "dr_vu_ftoi.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void word(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static uint32_t readword(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint32_t expected(uint32_t bits,unsigned shift){float f;memcpy(&f,&bits,4);double d=ldexp((double)f,(int)shift);if(isnan(d))return bits>>31?0x80000000u:0x7fffffffu;if(d>=2147483647.0)return 0x7fffffffu;if(d<=-2147483648.0)return 0x80000000u;return (uint32_t)(int32_t)d;}
int main(void){
 const uint32_t boundary[]={0,0x80000000,0x3f800000,0xbf800000,0x3fffffff,0xbfffffff,0x4effffff,0xceffffff,0x4f000000,0xcf000000,0x7f800000,0xff800000,0x7fc00000,0xffc00000,0x7fa00001,0xffa00001,0x7fffffff,0xffffffff,1,0x80000001,0x007fffff,0x807fffff};
 const unsigned shifts[]={0,4,12,15};unsigned cases=0;uint32_t seed=0x12345678;
 for(unsigned k=0;k<4;k++)for(unsigned row=0;row<5010;row++){
  unsigned char code[40]={0},data[16],out[16];uint32_t bits[4];
  for(unsigned j=0;j<4;j++){seed=seed*1664525u+1013904223u;unsigned idx=row*4+j;bits[j]=idx<sizeof(boundary)/sizeof(*boundary)?boundary[idx]:seed;word(data+j*4,bits[j]);assert(dr_vu_ftoi(bits[j],shifts[k])==expected(bits[j],shifts[k]));}
  /* LQ vf1; FTOIx vf2,vf1; SQ vf2; E-bit NOP and its delay slot. */
  word(code,15u<<21|1u<<16);word(code+4,0x2ff);
  word(code+8,0x8000033c);unsigned sp=0x14+k;word(code+12,15u<<21|2u<<16|1u<<11|(sp>>2)<<6|0x3c|(sp&3));
  word(code+16,1u<<25|15u<<21|2u<<11|1u);word(code+20,0x2ff);
  word(code+24,0x8000033c);word(code+28,0x400002ff);word(code+32,0x8000033c);word(code+36,0x2ff);
  dr_vu1_reset(NULL,NULL);assert(!dr_vu1_program(0,code,sizeof(code)));assert(!dr_vu1_unpack(0x6c,1,0,data,sizeof(data)));assert(!dr_vu1_call(0,16));assert(!dr_vu1_read(16,out,sizeof(out)));
  for(unsigned j=0;j<4;j++){assert(readword(out+j*4)==expected(bits[j],shifts[k]));cases++;}
 }
 printf("PASS %u full-interpreter FTOI lane cases; boundary/NaN/Inf/signed zero and random bit patterns\n",cases);
}
