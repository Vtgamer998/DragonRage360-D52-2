/* SPDX-License-Identifier: AGPL-3.0-or-later
 * 0.25: Dma_Alloc / Dma_AddData / Dma_AddRef / Dma_BeginDirect / Dma_EndDirect.
 * Byte comparison with the separately compiled, renamed upstream functions on
 * a LE host. A big-endian host is SIMULATED by forcing the swap and by storing
 * byte-reversed values the way a BE CPU stores native u32/u64. This is not a
 * run on PowerPC. */
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
extern void *Ref_Dma_Alloc(int32_t);
extern unsigned long *Ref_Dma_BeginDirect(void);
extern void Ref_Dma_EndDirect(unsigned long *);
extern void Ref_Dma_AddData(void *,int32_t);
extern void Ref_Dma_AddFrame(int32_t,int32_t,int32_t);
static DrGsStream stream;
static unsigned char *reference;
static unsigned comparisons,flushed;static int skip_consumer;
#define B(s) ((const unsigned char *)(s).bytes)
static unsigned char last_flush[0x1000];static size_t last_flush_size;
static int gif(void *a,const unsigned char *p,uint32_t n) {(void)a;(void)p;assert(n);return 0;}
static int submit(void *a,const DrGsSpan *s,uint32_t addr)
{
    flushed++;last_flush_size=s->size<sizeof last_flush?s->size:sizeof last_flush;
    memcpy(last_flush,s->bytes,last_flush_size);
    if(skip_consumer)return 0; /* payload is arbitrary bytes, only conversion is checked */
    return dr_gs_stream_chain(&stream,s,1,addr,1)+(a==NULL);
}
static uint32_t rng=0x9e3779b9;
static uint32_t rnd(void) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static uint64_t rnd64(void) {return (uint64_t)rnd()<<32|rnd();}
static uint32_t bs32(uint32_t x) {return __builtin_bswap32(x);}
static uint64_t bs64(uint64_t x) {return __builtin_bswap64(x);}
static int swapped;
static uint32_t st32(uint32_t v) {return swapped?bs32(v):v;} /* what a BE CPU stores */
static uint64_t st64(uint64_t v) {return swapped?bs64(v):v;}
static void open_producer(void)
{
    assert(!dr_gs_stream_init(&stream,gif,&stream));
    assert(!dr_dma_producer_open(submit,&stream));
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
static void run_alloc(unsigned words)
{
    size_t bytes=words*4;
    uint32_t *a=Dma_Alloc((int32_t)bytes),*b=Ref_Dma_Alloc((int32_t)bytes);
    assert(a && b);
    for(unsigned i=0;i<words;i++){uint32_t v=rnd();a[i]=st32(v);b[i]=v;}
}
static void run_direct(unsigned pairs)
{
    unsigned long long *a=Dma_BeginDirect();unsigned long *b=Ref_Dma_BeginDirect();
    assert(a && b);
    for(unsigned i=0;i<pairs*2;i++){uint64_t v=rnd64();a[i]=st64(v);b[i]=v;}
    Dma_EndDirect(a+pairs*2);Ref_Dma_EndDirect(b+pairs*2);
}
static void run_data(unsigned bytes,int32_t claimed)
{
    static uint32_t src[0x400],ref_src[0x400];
    assert(bytes<=sizeof src && !(bytes%4));
    for(unsigned i=0;i<bytes/4;i++){uint32_t v=rnd();ref_src[i]=v;src[i]=st32(v);}
    Dma_AddData(src,claimed);Ref_Dma_AddData(ref_src,claimed);
}
static void one_mode(int swap)
{
    swapped=swap;dr_dma_native_swap(swap);
    for(unsigned n=1;n<=70;n++){ /* Alloc: several sizes, including non multiples of 16 */
        reset();run_alloc(n*4);compare();
        reset();run_alloc(n*4+3);compare();
    }
    reset();run_alloc(0);compare();           /* size 0: no advance */
    reset();Dma_Alloc(15);Ref_Dma_Alloc(15);compare(); /* floors to 0 */
    for(unsigned n=1;n<=150;n+=7){reset();run_direct(n);compare();}
    reset();{unsigned long long *a=Dma_BeginDirect();unsigned long *b=Ref_Dma_BeginDirect();Dma_EndDirect(a);Ref_Dma_EndDirect(b);}compare(); /* empty */
    for(unsigned n=16;n<=512;n+=48){reset();run_data(n,(int32_t)n);compare();}
    reset();run_data(64,40);compare();       /* floors 40 -> 32 */
    reset();run_data(64,15);compare();       /* floors to 0, nothing queued */
    /* Mixed sequence with existing producers, all in order. */
    reset();Dma_AddFrame(10,10,0);Ref_Dma_AddFrame(10,10,0);
    run_alloc(60);run_direct(5);run_data(96,96);run_direct(3);run_alloc(12);
    assert(dr_dma_pending_regions()==4); /* Alloc, Direct, Direct, Alloc; AddData converts at once */
    compare(); /* view converts once; a second view must not convert again */
    {DrGsSpan s1,s2;assert(!dr_dma_producer_view(&s1));assert(dr_dma_pending_regions()==0);
     assert(!dr_dma_producer_view(&s2));assert(s1.size==s2.size);compare();}
}
static void test_ref(void)
{
    /* REF is copied behind a CNT tag; data stays RAW (already LE) by default. */
    swapped=1;dr_dma_native_swap(1);reset();
    unsigned char code[48];for(unsigned i=0;i<sizeof code;i++)code[i]=(unsigned char)rnd();
    Dma_AddRef(code,sizeof code);
    DrGsSpan s;assert(!dr_dma_producer_view(&s));assert(s.size==16+sizeof code);
    assert(B(s)[0]==3 && B(s)[1]==0 && B(s)[3]==0x10);
    assert(!memcmp(B(s)+16,code,sizeof code));
    /* Same bytes declared W32 are reversed word by word when swapping. */
    reset();dr_dma_add_ref_kind(code,sizeof code,DR_DMA_W32);assert(!dr_dma_producer_view(&s));
    for(unsigned i=0;i<sizeof code;i++)assert(B(s)[16+i]==code[(i&~3u)+3-(i&3)]);
    reset();dr_dma_native_swap(0);dr_dma_add_ref_kind(code,sizeof code,DR_DMA_W32);
    assert(!dr_dma_producer_view(&s));assert(!memcmp(B(s)+16,code,sizeof code));
    reset();Dma_AddRef(code,8);assert(!dr_dma_producer_view(&s));assert(s.size==16 && B(s)[0]==0); /* qwc 0 like the original */
    comparisons++;
}
static void test_errors(void)
{
    dr_dma_native_swap(0);
    reset();assert(Dma_Alloc(-1)==NULL && dr_dma_producer_error()==DR_DMA_INVALID);
    Dma_ResetBuffers(); /* error is sticky by design */
    assert(dr_dma_producer_close()==0);open_producer();
    /* Capacity: Alloc up to the limit, then fail into the sink, never beyond the buffer. */
    void *p=Dma_Alloc(0x80000);assert(p && !dr_dma_producer_error());
    p=Dma_Alloc(0x7FFC0);assert(p && !dr_dma_producer_error()); /* exactly fills BYTES-TAIL */
    p=Dma_Alloc(16);assert(p && dr_dma_producer_error()==DR_DMA_FULL);memset(p,0xAA,16); /* sink, no crash */
    assert(dr_dma_producer_close()==0);open_producer();
    /* BeginDirect needs 64 KiB of room; EndDirect rejects overruns and bad pointers. */
    unsigned long long *d=Dma_BeginDirect();assert(d && !dr_dma_producer_error());
    Dma_EndDirect(d+2*0x10000/16+4); /* past DIRECT_MAX */
    assert(dr_dma_producer_error()==DR_DMA_INVALID);
    assert(dr_dma_producer_close()==0);open_producer();
    Dma_Alloc(0xFFFC0-0x10000+0x10);
    d=Dma_BeginDirect();assert(d);assert(dr_dma_producer_error()==DR_DMA_FULL);d[0]=1;Dma_EndDirect(d+2); /* sink ignored */
    assert(dr_dma_producer_close()==0);open_producer();
    /* Pending-table overflow is an error, not memory corruption. */
    for(unsigned i=0;i<16385;i++)Dma_Alloc(16);
    assert(dr_dma_producer_error()==DR_DMA_FULL);
    assert(dr_dma_producer_close()==0);open_producer();
    /* Producers refuse to run closed. */
    assert(dr_dma_producer_close()==0);assert(Dma_Alloc(16)==NULL);assert(Dma_BeginDirect()==NULL);
    open_producer();comparisons+=6;
}
static void test_flush(void)
{
    dr_dma_native_swap(1);swapped=1;reset();flushed=0;skip_consumer=1;
    uint32_t *a=Dma_Alloc(16);a[0]=bs32(0x11223344);a[1]=bs32(0x55667788);
    Dma_Flush();assert(flushed==1 && !dr_dma_producer_error() && dr_dma_pending_regions()==0);
    assert(last_flush[0]==0x44 && last_flush[1]==0x33 && last_flush[2]==0x22 && last_flush[3]==0x11);
    assert(last_flush[4]==0x88 && last_flush[7]==0x55);
    skip_consumer=0;comparisons++;
}
int main(void)
{
    reference=mmap(NULL,0x100000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
    assert(reference!=MAP_FAILED && (uintptr_t)reference<=UINT32_MAX-0x100000);
    Ref_gDmaBuf[0]=(uint32_t)(uintptr_t)reference;Ref_gDmaBufEnd[0]=Ref_gDmaBuf[0]+0x100000;
    Ref_gDmaBufSize=0x100000;open_producer();
    one_mode(0);one_mode(1);
    test_ref();test_flush();test_errors();
    printf("PASS dma native: %u comparisons/checks\n",comparisons);
    return 0;
}
