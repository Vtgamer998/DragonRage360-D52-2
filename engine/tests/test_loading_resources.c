/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_resconv.h"
#include "xb_demo_resources.h"
#include "dr_texfile.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
void XbRes_TestRead(int,int,uint64_t,uint64_t,void *,size_t);
void *Heap_Alloc(int32_t n,int32_t a,int32_t b,int32_t c){(void)a;(void)b;(void)c;return malloc((size_t)n);}
void Heap_Free(void *p){free(p);}
static uint32_t be(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
int main(int argc,char **argv){
 assert(argc==4);assert(!XbRes_RegisterLoadingScreens());
 for(int i=0;i<3;i++) {
  FILE *f=fopen(argv[i+1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long bytes=ftell(f);assert(bytes>8 && bytes<=0x3000);rewind(f);
  uint8_t *packed=malloc((size_t)bytes);assert(packed && fread(packed,1,(size_t)bytes,f)==(size_t)bytes);fclose(f);
  uint32_t raw=XbRes_Le32(packed);assert(raw && raw<=0x6800);
  uint8_t *out=malloc(0x6800+16);assert(out);memset(out,0xcd,0x6800+16);
  assert(!XbRes_BeginCheckedReads());XbRes_TestRead(1,0x3c0+i,0,(uint64_t)bytes,packed,(size_t)bytes);
  int32_t actual=-1;assert(XbBpe_DecodeBounded(packed,out,0x6800,&actual)==out);assert(actual==(int32_t)raw && !XbRes_LastError());
  for(unsigned j=raw;j<0x6800+16;j++)assert(out[j]==0xcd);
  uint32_t count=XbRes_Le32(out),table=XbRes_Le32(out+4)*4;
  assert(count>=(i==1?3u:2u));
  /* Host is LE. Callback cleared pointer slots without changing packets.
   * A separate copy exercises the target big-endian encoding explicitly. */
  uint8_t *big=malloc(raw);assert(big);memcpy(big,out,raw);assert(!dr_texfile_prepare(big,raw,1));assert(be(big)==count && be(big+4)*4==table);
  assert(!memcmp(big+table+count*64,out+table+count*64,raw-table-count*64));
  for(unsigned t=0;t<count;t++) {
   size_t at=table+t*64;assert(be(big+at)==XbRes_Le32(out+at));assert(be(big+at+4)==XbRes_Le32(out+at+4));
   assert(be(big+at+0x30)==XbRes_Le32(out+at+0x34) && be(big+at+0x34)==XbRes_Le32(out+at+0x30));
   for(unsigned j=0;j<8;j++)assert(!big[at+0x38+j]);
  }
  /* Each rejection must leave every destination byte unchanged. */
  uint8_t *bad=malloc(raw),*before=malloc(raw);assert(bad && before);
  for(int mode=0;mode<4;mode++) {
   memcpy(bad,out,raw);
   if(mode==0)memset(bad,0,4);
   if(mode==1)memset(bad+4,0xff,4);
   if(mode==2)memset(bad+table,0xff,4);
   if(mode==3)bad[table+0x2c]=16;
   memcpy(before,bad,raw);assert(dr_texfile_prepare(bad,raw,1));assert(!memcmp(bad,before,raw));
  }
  printf("PASS loading id=0x%x packed=%ld raw=%u textures=%u; strict read+BPE+converter; BE metadata; packets preserved; 4 refusals atomic\n",0x3c1+i,bytes,raw,count);
  XbRes_EndCheckedReads();free(before);free(bad);free(big);free(out);free(packed);
 }
 return 0;
}
