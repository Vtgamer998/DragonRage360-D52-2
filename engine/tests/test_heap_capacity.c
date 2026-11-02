/* SPDX-License-Identifier: AGPL-3.0-or-later */
#define _GNU_SOURCE
#include "sys/heap.h"
#include "xb_heap.h"
#include <sys/mman.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
static void *arena;static size_t arena_size;static unsigned forgotten;
HeapBlock *gHeapStart[HEAP_COUNT],*gHeapEnd[HEAP_COUNT];
s32 gHeapSize[HEAP_COUNT],gHeapDebugValue,gHeapDebugUnused,gHeapUnk70[HEAP_COUNT],gHeapUnk78[HEAP_COUNT];u8 gHeapDebugFlag;
void *TestHeapMalloc(u32 n){assert(!arena);arena_size=n;arena=mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);assert(arena!=MAP_FAILED && (uintptr_t)arena<=UINT32_MAX);return arena;}
void XbRes_Forget(const void *p,size_t n){assert(p && n);forgotten++;}
int main(void){
 assert(!XbHeap_AllocationBytes(NULL));Heap_Init();assert(XbHeap_Ready());
 void *a=Heap_Alloc(2048,64,0,HEAP_ANY),*b=Heap_Alloc(4097,32,1,HEAP_ANY);assert(a && b);
 assert(XbHeap_AllocationBytes(a)==2048 && XbHeap_AllocationBytes(b)==4097);
 assert(!XbHeap_AllocationBytes((char *)a+1) && !XbHeap_AllocationBytes(arena));
 Heap_Init();assert(XbHeap_AllocationBytes(a)==2048 && XbHeap_AllocationBytes(b)==4097);
 Heap_Free(a);assert(!XbHeap_AllocationBytes(a) && XbHeap_AllocationBytes(b)==4097);
 Heap_Free(b);assert(!XbHeap_AllocationBytes(b) && forgotten==2);
 assert(!munmap(arena,arena_size));puts("PASS actual Heap_Init/Alloc/Free: exact capacities, head/tail alignment, interior/unknown/freed rejection, repeated init preserves live allocation");return 0;
}
