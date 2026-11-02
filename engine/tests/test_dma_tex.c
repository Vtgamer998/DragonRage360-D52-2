/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dr_dma_producer.h"
typedef union {uint64_t d[2];uint32_t w[4];} Q;
#define ARGS Q **,int,int,int,int,int,int,int,int,int,int,int,int,uint32_t,int
extern void Dma_PutTexStrips(ARGS);
extern void Ref_Dma_PutTexStrips(ARGS);
static int sink(void *p,const DrGsSpan *s,uint32_t a) {(void)p;(void)s;(void)a;return 0;}
static unsigned tests;
static void compare(int x0,int w,int u0,int n,int tail,int blend) {
    Q ref[512],*rp=ref;
    assert(!dr_dma_producer_open(sink,NULL));
    unsigned long long *raw=Dma_BeginDirect();Q *begin=(void *)raw,*actual=begin;
    memset(ref,0xAB,sizeof(ref));
    Ref_Dma_PutTexStrips(&rp,x0,0,x0+w,448,3,7,u0,0,u0+n*32+tail,448,8,8,0x80808080,blend);
    Dma_PutTexStrips(&actual,x0,0,x0+w,448,3,7,u0,0,u0+n*32+tail,448,8,8,0x80808080,blend);
    assert((char *)actual-(char *)begin==(char *)rp-(char *)ref);
    assert(!memcmp(begin,ref,(size_t)((char *)rp-(char *)ref)));
    assert(!dr_dma_producer_error());
    /* Simulate native big-endian u64 stores on alternate cases. This checks
     * canonicalization, not execution of big-endian instructions. */
    if(tests&1) {
        unsigned char *v=(void *)begin;size_t bytes=(size_t)((char *)actual-(char *)begin);
        for(size_t at=0;at<bytes;at+=8)for(unsigned i=0;i<4;i++) {
            unsigned char tmp=v[at+i];v[at+i]=v[at+7-i];v[at+7-i]=tmp;
        }
        dr_dma_native_swap(1);
    }
    Dma_EndDirect((void *)actual);
    DrGsSpan span;assert(!dr_dma_producer_view(&span));
    assert(span.size==16+(size_t)((char *)rp-(char *)ref));
    /* On this LE host, the serialized Direct payload must equal the original. */
    assert(!memcmp(span.bytes+16,ref,span.size-16));
    assert(!dr_dma_producer_close());dr_dma_native_swap(-1);tests++;
}
static void invalid(int which) {
    assert(!dr_dma_producer_open(sink,NULL));
    Q *base=(void *)Dma_BeginDirect(),*at=base;
    size_t len=64;memset(base,0xCD,len);
    if(which==0)Dma_PutTexStrips(&at,0,0,512,448,0,0,0,0,0,448,8,8,0,0);
    if(which==1)Dma_PutTexStrips(&at,0,0,512,448,0,0,0,0,31,448,8,8,0,0);
    if(which==2)Dma_PutTexStrips(&at,INT_MIN,0,INT_MAX,448,0,0,0,0,512,448,8,8,0,0);
    if(which==3){at=(Q *)((char *)base+65536-16);Dma_PutTexStrips(&at,0,0,512,448,0,0,0,0,512,448,8,8,0,0);assert(at==(Q *)((char *)base+65536-16));}
    if(which==4){Q other[40];memset(other,0xCD,sizeof(other));at=other;Dma_PutTexStrips(&at,0,0,512,448,0,0,0,0,512,448,8,8,0,0);assert(at==other);for(unsigned i=0;i<sizeof(other);i++)assert(((unsigned char *)other)[i]==0xCD);}
    if(which<3)assert(at==base);
    assert(dr_dma_producer_error()==DR_DMA_INVALID);
    for(unsigned i=0;i<len;i++)assert(((unsigned char *)base)[i]==0xCD);
    assert(!dr_dma_producer_close());dr_dma_native_swap(-1);tests++;
}
static int mapped_sink(void *p,const DrGsSpan *s,size_t n,uint32_t a)
{(void)p;(void)s;(void)n;(void)a;return 0;}
static void ownership(void) {
    _Alignas(16) Q map[40];uint32_t address;
    assert(!dr_dma_producer_open_mapped(mapped_sink,NULL));
    assert(!dr_dma_map_chain(map,sizeof(map),DR_DMA_W64,&address));
    assert(!dr_dma_w64_range(map,sizeof(map)));
    assert(!dr_dma_w64_range(map+39,16));
    assert(dr_dma_w64_range(map+39,32)==DR_DMA_INVALID);
    assert(!dr_dma_producer_close());tests++;
    assert(!dr_dma_producer_open_mapped(mapped_sink,NULL));
    assert(!dr_dma_map_chain(map,sizeof(map),DR_DMA_W32,&address));
    assert(dr_dma_w64_range(map,16)==DR_DMA_INVALID);
    assert(!dr_dma_producer_close());tests++;
    assert(!dr_dma_producer_open(sink,NULL));
    void *at=dr_dma_alloc_kind(64,DR_DMA_W64);
    assert(!dr_dma_w64_range(at,64));DrGsSpan span;
    assert(!dr_dma_producer_view(&span)); /* converted allocation is immutable */
    assert(dr_dma_w64_range(at,16)==DR_DMA_INVALID);
    assert(!dr_dma_producer_close());tests++;
}
int main(void) {
    /* Both exact-width and fractional branches, both blend modes, original
     * truncation for non-multiple-of-32 UV widths, nonzero screen/UV origins. */
    for(int n=1;n<=32;n++)for(int w=1;w<=600;w+=7)for(int b=0;b<2;b++)
        compare(13,w,32,n,(n+w)%32,b);
    for(int n=1;n<=16;n++)for(int w=32;w<=512;w+=32)compare(0,w,0,n,0,1);
    for(int i=0;i<5;i++)invalid(i);
    ownership();
    puts("Dma_PutTexStrips: byte-identical to original with PS2 software arithmetic; bounds errors do not write");
    printf("%u cases; host only, no GPU or combat execution\n",tests);
    return 0;
}
