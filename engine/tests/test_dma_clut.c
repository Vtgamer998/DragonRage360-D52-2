/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "dr_dma_producer.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static unsigned char expected[0x490];static unsigned submissions;
static void store(unsigned char*p,uint32_t v,int big){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*(big?3-i:i)));}
static int submit(void *ctx,const DrGsSpan*s,size_t n,uint32_t a){(void)ctx;assert(n==2 && s[0].address==a && s[1].size==sizeof(expected));assert(!memcmp(s[1].bytes,expected,sizeof(expected)));submissions++;return 0;}
int main(void){
 for(int big=0;big<2;big++){
  _Alignas(16) unsigned char packet[0x4c0];memset(packet,0,sizeof(packet));memset(expected,0,sizeof(expected));
  for(unsigned i=0;i<0x70;i+=4){uint32_t v=0x12340000+i;store(packet+i,v,big);store(expected+i,v,0);}
  for(unsigned i=0x70;i<0x470;i++)packet[i]=expected[i]=(unsigned char)(i*17);
  for(unsigned i=0x470;i<0x490;i+=4){uint32_t v=0x98760000+i;store(packet+i,v,big);store(expected+i,v,0);}
  store(packet+0x494,0x30000049,big);store(packet+0x498,(uint32_t)(uintptr_t)packet,big);
  assert(!dr_dma_producer_open_mapped(submit,NULL));dr_dma_native_swap(big);assert(!dr_dma_add_clut_ref(packet+0x494));assert(Dma_GetUsedSize()==16);memset(packet,0xCC,sizeof(packet));Dma_Flush();assert(!dr_dma_producer_error());assert(!dr_dma_producer_close());
 }
 _Alignas(16) unsigned char packet[0x4c0]={0};assert(!dr_dma_producer_open_mapped(submit,NULL));dr_dma_native_swap(0);assert(dr_dma_add_clut_ref(packet+0x494)==DR_DMA_INVALID && !Dma_GetUsedSize());assert(!dr_dma_producer_close());assert(submissions==2);
 puts("PASS CLUT ABI: native u32 prefix/tail, byte RGBA IMAGE preserved; LE and simulated BE exact bytes; stack snapshot immutable; invalid REF rejected atomically");return 0;
}
