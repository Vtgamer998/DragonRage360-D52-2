/* SPDX-License-Identifier: MIT
 * Actual game heap and native arena; host mmap substitutes only allocation
 * and log transport. No Xbox MMIO, ABI execution, models or fight are claimed. */
#define _GNU_SOURCE
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/mman.h>
#include "sys/heap.h"
#include "xb_heap.h"
#include "xb_mem.h"
#include "xb_resconv.h"
HeapBlock *gHeapStart[2],*gHeapEnd[2];s32 gHeapSize[2],gHeapDebugValue,gHeapDebugUnused,gHeapUnk70[2],gHeapUnk78[2];u8 gHeapDebugFlag;
extern s32 Heap_GetFreeSize(void),Heap_GetUsedSize(void);
extern int Port_StageBufSize(void);extern float Port_WideFactor(void);
extern void XbRes_TestRead(int,int,uint64_t,uint64_t,void *,size_t);
static int allocation_mode=1;static unsigned events,checks;
static struct {void *p;size_t n;int high;} regions[64];
void *XbTest_Calloc(size_t n,size_t size) {
    if(allocation_mode==1)return NULL;
    if(size && n>SIZE_MAX/size)return NULL;
    size_t bytes=(n*size+4095)&~(size_t)4095;void *p;
    if(allocation_mode==2)p=calloc(n,size);
    else {
        p=NULL;
        /* Non-fixed hints work on both ARM64 and x86_64 hosts. Never replace
         * another mapping. Accept only a complete arena below 4 GiB. */
        for(uintptr_t hint=0x30000000;hint<0x70000000;hint+=0x4000000) {
            void *candidate=mmap((void *)hint,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
            if(candidate==MAP_FAILED)continue;
            if((uintptr_t)candidate<=UINT32_MAX && (uint64_t)(uintptr_t)candidate+bytes<=0x100000000ull){p=candidate;break;}
            assert(!munmap(candidate,bytes));
        }
        if(!p)return NULL;
    }
    assert(p);
    for(unsigned i=0;i<64;i++)if(!regions[i].p){regions[i].p=p;regions[i].n=bytes;regions[i].high=allocation_mode==2;return p;}
    abort();
}
void XbTest_Free(void *p) {
    if(!p)return;
    for(unsigned i=0;i<64;i++)if(regions[i].p==p) {
        if(regions[i].high)free(p);else assert(!munmap(p,regions[i].n));
        regions[i].p=NULL;return;
    }
    abort();
}
void xsdk_log_write(const char *fmt,...) {
    char line[512];va_list ap;va_start(ap,fmt);vsnprintf(line,sizeof(line),fmt,ap);va_end(ap);
    assert(line[0]);events++;
}
static void valid_chain(void) {
    HeapBlock *b=gHeapStart[0];uint64_t total=0;unsigned n=0;
    while(b && b!=gHeapEnd[0]) {
        assert(++n<4096 && b->magic==HEAP_MAGIC && b->size>=36 && !(b->size&3));
        assert((uintptr_t)b+b->size<=(uintptr_t)gHeapEnd[0]);
        assert(*(uint32_t *)((uint8_t *)b+b->size-4)==(uint32_t)b->size);
        if(b->used) {
            assert(b->data>=(uintptr_t)b+sizeof(*b) && !(b->data%b->align));
            assert((uint64_t)b->data+b->dataSize<=(uintptr_t)b+b->size-4);
        }
        total+=(unsigned)b->size;b=(HeapBlock *)((uint8_t *)b+b->size);
    }
    assert(b==gHeapEnd[0] && total==(unsigned)gHeapSize[0]);checks++;
}
static uint32_t random_state=360;
static uint32_t rng(void){random_state=random_state*1664525u+1013904223u;return random_state;}
int main(void) {
    assert(Port_StageBufSize()==0x6cb800 && Port_WideFactor()==1.0f);
    assert(XbMem_SelfTest()>0); /* before Init: never touch unmapped MMIO */
    checks++;
    Heap_Init();assert(!XbHeap_Ready() && XbHeap_LastError()==XB_HEAP_NOT_READY && !gHeapStart[0]);
    assert(!Heap_Alloc(32,32,0,2) && XbHeap_LastError()==XB_HEAP_NOT_READY);checks++;
    assert(XbMem_InitHeap()!=0);allocation_mode=2;
    /* A normal 64-bit host allocation cannot be represented in game fields. */
    assert(XbMem_InitHeap()!=0);allocation_mode=0;assert(!XbMem_InitHeap());
    assert(!Port_Malloc(0) && !Port_Malloc(UINT32_MAX));Heap_Init();
    assert(XbHeap_Ready() && !XbHeap_LastError() && gHeapSize[0]==(s32)XB_GAME_HEAP_BYTES);
    assert(Heap_GetFreeSize()==gHeapSize[0]-32);valid_chain();
    void *p=Heap_Alloc(64,64,0,2);assert(p && !((uintptr_t)p&63));memset(p,0x9c,64);
    HeapBlock *start=gHeapStart[0];Heap_Init();assert(gHeapStart[0]==start);
    for(unsigned i=0;i<64;i++)assert(((uint8_t *)p)[i]==0x9c);
    XbRes_TestRead(0,0,0,64,p,64);assert(XbRes_SourceSize(p)==64);
    Heap_Free(p);assert(!XbRes_SourceSize(p));valid_chain();
    Heap_Free(p);assert(XbHeap_LastError()==XB_HEAP_BAD_FREE);valid_chain();
    Heap_Free(NULL);assert(!XbHeap_LastError());
    assert(!Heap_Alloc(10,0,0,2) && XbHeap_LastError()==XB_HEAP_ARGUMENT);
    assert(!Heap_Alloc(INT_MAX,32,0,2) && XbHeap_LastError()==XB_HEAP_ARGUMENT);
    assert(!Heap_Alloc(10,UINT32_MAX,0,2) && XbHeap_LastError()==XB_HEAP_ARGUMENT);
    assert(!Heap_Alloc(10,32,0,-1) && XbHeap_LastError()==XB_HEAP_ARGUMENT);
    assert(!Heap_Alloc(10,32,0,3) && XbHeap_LastError()==XB_HEAP_ARGUMENT);
    checks+=8;
    struct Slot {uint8_t *p;unsigned size,byte;} slots[128]={0};
    for(unsigned turn=0;turn<3000;turn++) {
        unsigned at=rng()%128;struct Slot *s=&slots[at];
        if(s->p) {
            for(unsigned k=0;k<s->size;k++)assert(s->p[k]==s->byte);
            Heap_Free(s->p);assert(!XbHeap_LastError());s->p=NULL;
        } else {
            s->size=1+rng()%8192;s->byte=1+rng()%254;
            unsigned align=1u<<(2+rng()%11);
            s->p=Heap_Alloc((s32)s->size,align,(s32)(rng()&1),2);
            assert(s->p && !((uintptr_t)s->p%align));memset(s->p,(int)s->byte,s->size);
            for(unsigned j=0;j<128;j++)if(j!=at && slots[j].p)
                assert(s->p+s->size<=slots[j].p || slots[j].p+slots[j].size<=s->p);
        }
        valid_chain();
    }
    for(unsigned i=0;i<128;i++)if(slots[i].p)Heap_Free(slots[i].p);
    assert(Heap_GetUsedSize()==0 && Heap_GetFreeSize()==gHeapSize[0]-32);valid_chain();
    assert(!Heap_Alloc((s32)XB_GAME_HEAP_BYTES,32,0,2) && XbHeap_LastError()==XB_HEAP_FULL);
    unsigned big=Port_GameBigAlloc(2048);assert(big);
    p=(void *)(uintptr_t)big;XbRes_TestRead(0,0,0,2048,p,2048);assert(XbRes_SourceSize(p)==2048);
    Heap_Free(p);assert(!XbHeap_LastError() && !XbRes_SourceSize(p));checks++;
    /* Exhaust only the arena's remaining bump space. A repeated initializer
     * must not rewind it or allow writes past the end. */
    unsigned filled=0;while(Port_Malloc(16)){assert(++filled<200000);}
    assert(filled && !Port_Malloc(16) && !XbMem_InitHeap() && !Port_Malloc(16));checks++;
    assert(events>0);printf("PASS: %u heap checks / 3000 randomized head-tail operations; original Heap_* code, low 32-bit arena, failure paths, alignment, payloads, coalescing, idempotent init, exhaustion and converter lifetime; host only\n",checks);
    for(unsigned i=0;i<64;i++)if(regions[i].p)XbTest_Free(regions[i].p);
}
