/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "dr_dma_producer.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static unsigned submissions;static unsigned mode;
static uint32_t le(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int submit(void *ctx,const DrGsSpan *s,size_t n,uint32_t address){
 (void)ctx;assert(n==2 && s[0].address==address && s[1].address==0x43000000);submissions++;
 if(mode==1){assert(s[1].size==32);const unsigned char*p=s[1].bytes;for(unsigned i=0;i<32;i++)assert(p[i]==(unsigned char)i);assert(le(s[0].bytes)==0x30000002 && le((const unsigned char*)s[0].bytes+4)==s[1].address);}
 if(mode==2){assert(s[1].size==0x200000 && s[0].size==32*16+64);const unsigned char*p=s[1].bytes;for(unsigned i=0;i<s[1].size;i++)assert(p[i]==0xA5);}
 return 0;
}
int main(void){
 for(int be=0;be<2;be++){
  assert(!dr_dma_producer_open_mapped(submit,NULL));dr_dma_native_swap(be);mode=1;
  unsigned char data[32];for(unsigned i=0;i<32;i++)data[i]=(unsigned char)(be?(i^3):i);
  dr_dma_add_ref_kind(data,32,DR_DMA_W32);memset(data,0xCC,32);Dma_Flush();assert(!dr_dma_producer_error());assert(!dr_dma_producer_close());
 }
 assert(!dr_dma_producer_open_mapped(submit,NULL));dr_dma_native_swap(0);mode=2;
 unsigned char *payload=malloc(65536);assert(payload);memset(payload,0xA5,65536);
 for(unsigned i=0;i<32;i++)Dma_AddRef(payload,65536);
 assert(!dr_dma_producer_error() && Dma_GetUsedSize()==512);memset(payload,0,65536);Dma_Flush();assert(!dr_dma_producer_error());assert(!dr_dma_producer_close());
 assert(!dr_dma_producer_open_mapped(submit,NULL));
 for(unsigned i=0;i<128;i++)Dma_AddRef(payload,65536);
 assert(!dr_dma_producer_error() && Dma_GetUsedSize()==2048);
 Dma_AddRef(payload,16);assert(dr_dma_producer_error()==DR_DMA_FULL && Dma_GetUsedSize()==2048);Dma_Flush();assert(submissions==3);assert(!dr_dma_producer_close());
 assert(!dr_dma_producer_open_mapped(submit,NULL));Dma_AddRef(payload,65536);Dma_ResetBuffers();Dma_AddRef(payload,65536);assert(!dr_dma_producer_error());assert(!dr_dma_producer_close());free(payload);
 puts("PASS REF snapshots: canonical LE and simulated BE; immutable source; 2 MiB payload in 512 command bytes; exact 8 MiB cap rejects atomically; reset/failed flush/close ownership");return 0;
}
