/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "dr_dma_producer.h"
#include "xb_iop.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static DrGsStream stream;static unsigned submits;
void *Port_LowAlloc(size_t n){return calloc(1,n);}
void Port_LowFree(void *p){free(p);}
static int gif(void *opaque,const unsigned char *p,uint32_t n){(void)opaque;assert(p && n);return 0;}
static int submit(void *opaque,const DrGsSpan *spans,size_t n,uint32_t start){
    assert(opaque==&stream && n==2 && spans[1].size==32);
    assert(!dr_gs_stream_chain(&stream,spans,n,start,1));
    assert(stream.stats.tags==5);submits++;return 0;
}
static uint32_t native(uint32_t x,int big){return big?__builtin_bswap32(x):x;}
static void mapped(int big){
    _Alignas(16) uint32_t ext[8]={0};uint32_t address,interior;
    ext[0]=native(0x10000000,big);ext[4]=native(0x20000000,big);
    assert(!dr_gs_stream_init(&stream,gif,NULL));
    assert(!dr_dma_producer_open_mapped(submit,&stream));dr_dma_native_swap(big);
    assert(!dr_dma_map_chain(ext,sizeof(ext),DR_DMA_W32,&address));assert(address==0x42000000);
    assert(!dr_dma_chain_address(ext+4,&interior) && interior==address+16);
    Dma_AddNext(ext,ext+4);assert(!dr_dma_producer_error());
    DrGsSpan root;assert(!dr_dma_producer_view(&root));assert(root.size==16);
    assert(((const unsigned char *)root.bytes)[3]==0x20);
    uint32_t resume;memcpy(&resume,ext+5,4);assert(resume==native(root.address+16,big));
    Dma_Flush();assert(!dr_dma_producer_error());assert(ext[0]==native(0x10000000,big));
    /* The borrowed map expires after Flush, so stale pointers cannot be reused. */
    assert(dr_dma_chain_address(ext,&address)==DR_DMA_INVALID);
    assert(!dr_dma_producer_close());
}
static int snapshot_submit(void *opaque,const DrGsSpan *spans,size_t n,uint32_t start){
 (void)opaque;(void)start;assert(n==2 && spans[1].address==0x43000000u && spans[1].size==64);
 const unsigned char*p=spans[1].bytes;assert(p[0]==3&&p[1]==0&&p[2]==0&&p[3]==0x60);
 assert(p[16]==0x44&&p[17]==0x33&&p[18]==0x22&&p[19]==0x11);return 0;
}
static void snapshot_words(int big){
 _Alignas(16) uint32_t ext[16]={0};uint32_t address;ext[0]=native(0x60000003,big);ext[4]=native(0x11223344,big);
 assert(!dr_dma_producer_open_mapped(snapshot_submit,NULL));dr_dma_native_swap(big);
 assert(!dr_dma_snapshot_words(ext,sizeof(ext),&address)&&address==0x43000000u);
 memset(ext,0,sizeof(ext));Dma_Flush();assert(!dr_dma_producer_error());assert(!dr_dma_producer_close());
 assert(!dr_dma_producer_open_mapped(snapshot_submit,NULL));assert(dr_dma_snapshot_words(ext,15,&address)==DR_DMA_INVALID);assert(!dr_dma_producer_close());
}
int main(void){
    mapped(0);mapped(1);assert(submits==2);snapshot_words(0);snapshot_words(1);
    _Alignas(16) uint32_t ext[8]={0};uint32_t a;
    assert(!dr_dma_producer_open_mapped(submit,&stream));
    assert(!dr_dma_map_chain(ext,sizeof(ext),DR_DMA_RAW,&a));
    assert(dr_dma_map_chain(ext+4,16,DR_DMA_RAW,&a)==DR_DMA_INVALID);assert(!dr_dma_producer_close());
    assert(!dr_dma_producer_open_mapped(submit,&stream));Dma_AddNext(NULL,NULL);
    assert(dr_dma_producer_error()==DR_DMA_INVALID);assert(!dr_dma_producer_close());
    assert(!dr_dma_producer_open_mapped(submit,&stream));
    unsigned long long *d=Dma_BeginDirect();assert(d && !((uintptr_t)d&15));
    assert(Dma_Alloc(16)!=NULL && dr_dma_producer_error()==DR_DMA_STATE);assert(!dr_dma_producer_close());
    assert(!dr_dma_producer_open_mapped(submit,&stream));Dma_EndDirect((void *)ext);
    assert(dr_dma_producer_error()==DR_DMA_STATE);assert(!dr_dma_producer_close());
    /* Owned IOP buffers: actual bytes copied, interior range checked, no RPC. */
    unsigned char src[32];for(unsigned i=0;i<32;i++)src[i]=(unsigned char)i;
    unsigned char *p=sceSifAllocIopHeap(32);assert(p);Dma_SendToIop(p,src,32);
    assert(!XbIop_LastError() && !memcmp(p,src,32));
    assert(!XbIop_Copy(p+8,src,24));assert(!memcmp(p+8,src,24));
    unsigned char before[32];memcpy(before,p,32);
    assert(XbIop_Copy(p+9,src,24)==-1 && !memcmp(before,p,32));
    assert(sceSifFreeIopHeap(p+1)==-1);assert(!sceSifFreeIopHeap(p));
    assert(XbIop_Copy(p,src,1)==-1);assert(sceSifAllocIopHeap(-1)==NULL);
    puts("PASS: mapped NEXT/return, LE and simulated BE, ownership, Direct state and bounded IOP copies (host)");return 0;
}
