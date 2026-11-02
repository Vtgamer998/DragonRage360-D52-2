/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "dr_dma_producer.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static unsigned submits;
static unsigned char expected[48];
static void store(unsigned char *p,uint64_t v,unsigned n,int be){for(unsigned i=0;i<n;i++)p[be?n-1-i:i]=(unsigned char)(v>>(i*8));}
static int submit(void *opaque,const DrGsSpan *s,uint32_t address){
 (void)opaque;assert(s->address==address && s->size>=64);
 assert(!memcmp(s->bytes,expected,48));submits++;return 0;
}
int main(void){
 const uint32_t words[4]={0x10000002,0,0x10000000,0x50000002};
 const uint64_t gif[4]={0x1000000000008001ull,14,0x0013000000000000ull,0x50};
 for(unsigned i=0;i<4;i++)store(expected+i*4,words[i],4,0);
 for(unsigned i=0;i<4;i++)store(expected+16+i*8,gif[i],8,0);
 for(int be=0;be<=1;be++){
  unsigned char native[48],before[48];
  for(unsigned i=0;i<4;i++)store(native+i*4,words[i],4,be);
  for(unsigned i=0;i<4;i++)store(native+16+i*8,gif[i],8,be);
  memcpy(before,native,48);dr_dma_native_swap(be);
  assert(!dr_dma_producer_open(submit,NULL));assert(!dr_dma_add_texture_header(native));
  DrGsSpan view;assert(!dr_dma_producer_view(&view));assert(view.size==48 && !memcmp(view.bytes,expected,48));
  assert(!memcmp(before,native,48));Dma_Flush();assert(!dr_dma_producer_error());
  assert(!dr_dma_producer_close());
 }
 assert(submits==2);dr_dma_native_swap(-1);
 puts("PASS native TexBltPacket: four u32 then four u64, LE and simulated BE, canonical DMA snapshot; immutable input, no GPU");
 return 0;
}
