/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "dr_dma_producer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { IMAGE=512*224*3, PACKET=IMAGE+0x90 };
static unsigned char expected[PACKET];
static unsigned calls;
static void put(unsigned char *p,unsigned n,uint64_t v,int big) {
    for(unsigned i=0;i<n;i++)p[i]=(unsigned char)(v>>((big?n-1-i:i)*8));
}
static void packet(unsigned char *p,unsigned char *ref,int big) {
    memset(p,0,PACKET);memset(ref,0,16);
    put(ref,4,0x30000000u+IMAGE/16+9,big);
    put(ref+4,4,(uint32_t)(uintptr_t)p,big);
    put(p+12,4,0x50000000u+IMAGE/16+8,big);
    put(p+0x10,8,0x1000000000008004ull,big);put(p+0x18,8,14,big);
    put(p+0x20,8,0x01081c0001000000ull,big);
    for(unsigned i=0;i<4;i++)put(p+0x28+i*16,8,0x50+i,big);
    put(p+0x40,8,512ull+(224ull<<32),big);
    put(p+0x60,8,(IMAGE/16|0x8000ull)+(2ull<<58),big);
    for(unsigned i=0;i<IMAGE;i++)p[0x70+i]=(unsigned char)(i*17u+(i>>10));
    put(p+0x70+IMAGE,8,0x1000000000008001ull,big);
    put(p+0x78+IMAGE,8,14,big);put(p+0x88+IMAGE,8,0x3f,big);
}
static int gif(void *o,const unsigned char *p,uint32_t n){(void)o;assert(p && n);return 0;}
static int submit(void *o,const DrGsSpan *spans,size_t n,uint32_t start) {
    (void)o;assert(n==2 && spans[1].size==PACKET);
    assert(!memcmp(spans[1].bytes,expected,PACKET));
    DrGsStream stream;assert(!dr_gs_stream_init(&stream,gif,NULL));
    assert(!dr_gs_stream_chain(&stream,spans,n,start,1));calls++;return 0;
}
int main(void) {
    unsigned char *src=malloc(PACKET),ref[16],dummy[16];assert(src);
    packet(expected,dummy,0);
    for(int big=0;big<2;big++) {
        packet(src,ref,big);assert(!dr_dma_producer_open_mapped(submit,NULL));
        dr_dma_native_swap(big);assert(!dr_dma_add_xfade_ref(ref,src,PACKET));
        memset(src,0xCC,PACKET); /* producer must own a snapshot */
        Dma_Flush();assert(!dr_dma_producer_error());assert(!dr_dma_producer_close());
        for(unsigned bad=0;bad<28;bad++) {
            packet(src,ref,big);assert(!dr_dma_producer_open_mapped(submit,NULL));
            dr_dma_native_swap(big);size_t capacity=PACKET;
            if(bad<4)ref[bad*4]^=1;
            else if(bad<9)src[(unsigned[]){0x28,0x38,0x48,0x58,0x54088}[bad-4]]^=1;
            else if(bad==9)capacity--;
            else if(bad==10)src[0x40]^=1;
            else if(bad==11)src[0x60]^=1;
            else src[(unsigned[]){0x10,0x14,0x18,0x1c,0x20,0x24,0x30,0x34,0x50,0x54,0x68,0x6c,IMAGE+0x70,IMAGE+0x78,IMAGE+0x80,IMAGE+0x84}[bad-12]+(big?0:3)]^=0x80;
            assert(dr_dma_add_xfade_ref(ref,src,capacity)==DR_DMA_INVALID);
            assert(Dma_GetUsedSize()==0);Dma_Flush();assert(!dr_dma_producer_close());
        }
    }
    assert(calls==2);
    packet(src,ref,0);assert(!dr_dma_producer_open_mapped(submit,NULL));
    dr_dma_native_swap(0);unsigned accepted=0;
    while(accepted<32 && !dr_dma_add_xfade_ref(ref,src,PACKET))accepted++;
    assert(accepted==0x800000/PACKET && dr_dma_producer_error()==DR_DMA_FULL);
    Dma_Flush();assert(calls==2);assert(!dr_dma_producer_close());
    free(src);
    puts("PASS: RGB24 immutable snapshots through DMA/VIF in LE/BE; 56 malformed/short packets refused; safe close/reopen");
    return 0;
}
