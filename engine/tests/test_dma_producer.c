/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Byte comparison with actual upstream producer functions on a LE host.
 * Original functions are separately compiled and renamed, not reimplemented.
 * Only their packet-building paths run; no PS2 MMIO is executed. */
#define _GNU_SOURCE
#include "dr_dma_producer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
uint32_t Ref_gDmaBuf[2],Ref_gDmaBufEnd[2];
int32_t Ref_gDmaBufSize,Ref_gDmaBufIdx,Ref_gDmaVif1Started;
uint32_t *Ref_gDmaCur;
void FlushCache(int32_t mode) {assert(!mode);}
extern void Ref_Dma_AddTexFlush(void);
extern void Ref_Dma_AddFrame(int32_t,int32_t,int32_t);
extern void Ref_Dma_AddClamp(int32_t,int32_t,int32_t,int32_t,int32_t,int32_t);
extern void Ref_Dma_AddScissor(int32_t,int32_t,int32_t,int32_t);
extern void Ref_Dma_AddXyOffset(int32_t,int32_t);
extern void Ref_Dma_AddZbuf(int32_t,int32_t);
extern void Ref_Dma_AddTex0(unsigned long long);
extern void Ref_Dma_AddTex1(unsigned long long);
extern void Ref_Dma_AddFba(int32_t);
extern void Ref_Dma_AddColClamp(unsigned long long);
extern void Ref_Dma_AddAlpha(unsigned long long);
extern void Ref_Dma_AddFillRect(int32_t,int32_t,int32_t,int32_t,uint32_t);
extern void Ref_Port_GsMarker(int);
static DrGsStream stream;
static unsigned submits,gif_qwc;
static uint32_t submitted_address;
static int submit_error;
static unsigned char *reference;
static unsigned comparisons;
static int gif(void *arg,const unsigned char *p,uint32_t n)
{assert(arg==&stream && p && n);gif_qwc+=n;return 0;}
static int submit(void *arg,const DrGsSpan *span,uint32_t address)
{
    assert(arg==&stream && span && span->address==address);
    submits++;submitted_address=address;
    if(submit_error)return submit_error;
    return dr_gs_stream_chain(&stream,span,1,address,1);
}
static void open_producer(void)
{
    assert(!dr_gs_stream_init(&stream,gif,&stream));
    assert(!dr_dma_producer_open(submit,&stream));
    assert(dr_dma_producer_open(submit,&stream)==DR_DMA_STATE);
    assert(!dr_dma_producer_error());
}
static void reset(void)
{Dma_ResetBuffers();Ref_gDmaCur=(uint32_t *)reference;memset(reference,0,0x100000);}
static void compare(void)
{
    DrGsSpan span;assert(!dr_dma_producer_view(&span));
    size_t n=(unsigned char *)Ref_gDmaCur-reference;
    assert(!dr_dma_producer_error() && span.size==n);
    assert(!memcmp(span.bytes,reference,n));comparisons++;
}
#define PAIR(name,...) do {reset();Dma_##name(__VA_ARGS__);Ref_Dma_##name(__VA_ARGS__);compare();} while(0)
static uint32_t rng=0x12345678;
static uint32_t random_word(void) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(void)
{
    assert(dr_dma_producer_close()==DR_DMA_STATE);
    reference=mmap(NULL,0x100000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
    assert(reference!=MAP_FAILED && (uintptr_t)reference<=UINT32_MAX-0x100000);
    Ref_gDmaBuf[0]=(uint32_t)(uintptr_t)reference;Ref_gDmaBufEnd[0]=Ref_gDmaBuf[0]+0x100000;
    Ref_gDmaBufSize=0x100000;open_producer();
    PAIR(AddTexFlush);
    for(unsigned i=0;i<512;i++) {int effect=(int)random_word();reset();Port_GsMarker(effect);Ref_Port_GsMarker(effect);compare();}
    for(unsigned i=0;i<128;i++) {
        uint32_t r=random_word(),t=random_word();unsigned long long wide=(unsigned long long)r<<32|t;
        PAIR(AddFrame,r%512,t%64,(int32_t)(r^t));
        PAIR(AddClamp,r%4,t%4,(r>>2)%1024,(r>>12)%1024,(t>>2)%1024,(t>>12)%1024);
        int32_t x=(int32_t)(r%1024),y=(int32_t)(t%1024);
        PAIR(AddScissor,x,x+(int32_t)((r>>10)%1024),y,y+(int32_t)((t>>10)%1024));
        PAIR(AddXyOffset,r%4096,t%4096);
        PAIR(AddZbuf,r%512,t%2);
        PAIR(AddTex0,wide);PAIR(AddTex1,wide);PAIR(AddFba,r%2);
        PAIR(AddColClamp,wide);PAIR(AddAlpha,wide);
    }
    for(int width=32;width<=512;width+=32) {
        PAIR(AddFillRect,1792,1824,width,448,0x80ab1299u);
        PAIR(AddFillRect,0,0,width,1,0xffffffffu);
    }
    const int widths[]={0,1,31,33,47,63,95};
    for(unsigned i=0;i<sizeof(widths)/sizeof(*widths);i++) {
        PAIR(AddFillRect,1792,1824,widths[i],0,0x80402010u);
        PAIR(AddFillRect,0,0,widths[i],449,0x80402010u);
    }
    PAIR(AddScissor,50,10,100,20); // Reversed scissor is a valid empty GS window.
    // A concatenated frame is also identical, preserving register order.
    reset();Dma_AddFrame(0,8,-1);Ref_Dma_AddFrame(0,8,-1);
    Dma_AddScissor(0,511,0,447);Ref_Dma_AddScissor(0,511,0,447);
    Dma_AddXyOffset(0,0);Ref_Dma_AddXyOffset(0,0);
    Dma_AddFillRect(0,0,512,448,0x80402010);Ref_Dma_AddFillRect(0,0,512,448,0x80402010);compare();
    unsigned before=submits;Dma_Flush();assert(!dr_dma_producer_error() && submits==before+1);
    assert(submitted_address==0x41000000 && stream.stats.tags==6 && gif_qwc>0);
    assert(Dma_GetUsedSize()==0 && Dma_GetFreeSize()==Dma_GetBufSize());
    Dma_Flush();assert(!dr_dma_producer_error() && submitted_address==0x41100000);
    assert(!dr_dma_producer_close());
    // Capacity and invalid parameters must not publish a partial packet.
    open_producer();Dma_AddFrame(0,8,0);size_t used=(size_t)Dma_GetUsedSize();
    Dma_AddFillRect(0,0,-1,448,0);assert(dr_dma_producer_error()==DR_DMA_INVALID && (size_t)Dma_GetUsedSize()==used);
    before=submits;Dma_Flush();assert(submits==before);Dma_ResetBuffers();assert(dr_dma_producer_error()==DR_DMA_INVALID);
    assert(!dr_dma_producer_close());open_producer();
    for(unsigned i=0;i<100000 && !dr_dma_producer_error();i++)Dma_AddTexFlush();
    assert(dr_dma_producer_error()==DR_DMA_FULL && Dma_GetUsedSize()<=Dma_GetBufSize());
    before=submits;Dma_Flush();assert(submits==before);assert(!dr_dma_producer_close());
    open_producer();Dma_AddFrame(0,8,0);DrGsSpan queued;assert(!dr_dma_producer_view(&queued));
    submit_error=-777;Dma_Flush();assert(dr_dma_producer_error()==-777);
    DrGsSpan failed;assert(!dr_dma_producer_view(&failed) && failed.address==queued.address && failed.size==queued.size);
    before=submits;Dma_Flush();assert(submits==before);submit_error=0;assert(!dr_dma_producer_close());
    open_producer();uint32_t words[4]={0x70000000,0,0,0};
    assert(!dr_dma_append_words(words,4));assert(!dr_dma_producer_view(&queued));
    const unsigned char expected[16]={0,0,0,0x70};assert(queued.size==16 && !memcmp(queued.bytes,expected,16));
    unsigned char canonical[17]={0};memcpy(canonical+1,expected,16);
    assert(!dr_dma_append_canonical(canonical+1,16));assert(!dr_dma_producer_view(&queued));
    assert(queued.size==32 && !memcmp((const unsigned char *)queued.bytes+16,expected,16));
    assert(dr_dma_append_canonical(canonical,3)==DR_DMA_INVALID);
    assert(!dr_dma_producer_close());assert(!munmap(reference,0x100000));
    assert(comparisons==1841);
    printf("PASS: %u byte comparisons with original upstream DMA producers; canonical serialization, actual stream submission, FINISH/END, double buffers, bounds/capacity/sticky errors; no battle or GPU execution\n",comparisons);
}
