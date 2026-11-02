/* SPDX-License-Identifier: MIT
 * Memory of the Xbox 360 build: counterpart of port/src/plat_mem.c of the PC port.
 *
 * The game was written for 32 MB at fixed addresses and stores pointers in 32-bit fields. The Xbox 360 ABI has
 * 32-bit pointers already, so unlike the 64-bit PC and Android builds nothing here needs ptr32 rewriting. What
 * the game does need at fixed numbers (the PC port maps them the same way, plat_mem.c):
 *   0x10000000  64 KB  hardware registers (timers, DMA): plain memory, reads give what was last written
 *   0x12000000   8 KB  GS registers
 *   0x70000000  16 KB  scratchpad
 * The game heap does NOT need its PS2 address: the PC port's Android build moves it ("nothing in the game depends
 * on it, Port_Malloc starts wherever the region is"), so it is just allocated here.
 *
 * TODO-VERIFICAR (only the first Xbox run can tell): whether the kernel hands out these fixed low addresses.
 * XB_USE_NTALLOC enables the attempt, with NtAllocateVirtualMemory(&base, &size, type, protect, debug) in the
 * argument order of the Xenia emulator's implementation; check it against xsdk/kernel.h before the first build.
 * If it fails, XbMem_Init logs which region and returns its bit; the fallback is XB_ADDR_WINDOW (xb_port_addr.h),
 * which also needs the game's numeric-address accesses rewritten.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xb_mem.h"
#include "xb_resconv.h"
#include "xb_port_addr.h"
#include "xsdk/log.h"
#ifdef XB_USE_NTALLOC
#include "xsdk/kernel.h"
#endif

#define HEAP_FIRST_OFFSET 0x730u /* the PS2's malloc returns 0x3BE730 for the game heap: 0x730 into its page */
#define HEAP_SIZE (0x02000000u - 0x003BE000u)

int gPortScratchpadMapped;
#ifdef XB_ADDR_WINDOW
uintptr_t gXbWindow;
#endif

static uint8_t *sHeapRaw, *sHeapBase;
static unsigned sFailed=15; /* no fixed region is owned before XbMem_Init */
static uint32_t sHeapNext, sHeapEnd; /* offsets into sHeapBase */

typedef struct Region {
    uint32_t addr, size;
    const char *what;
    unsigned bit;
} Region;

static const Region kFixed[] = {
    {0x10000000u, 0x10000u, "hardware registers", 2},
    {0x12000000u, 0x10000u, "GS registers (8 KB used, mapped by 64 KB page)", 4},
    {0x70000000u, 0x10000u, "scratchpad (16 KB used, mapped by 64 KB page)", 8},
};

/* Takes [addr, addr+size) at exactly that address, zeroed. 0 = done. */
static int fixed_map(uint32_t addr, uint32_t size, const char *what) {
#ifdef XB_USE_NTALLOC
    void *base = (void *)(uintptr_t)addr;
    uint32_t sz = (size + 0xFFFFu) & ~0xFFFFu; /* 64 KB pages */
    long status = (long)NtAllocateVirtualMemory(&base, &sz, 0x3000u /* MEM_RESERVE|MEM_COMMIT */, 0x04u /* PAGE_READWRITE */, 0);

    if (status < 0 || (uintptr_t)base != addr) {
        xsdk_log_write("mem: %s at 0x%08X: refused (status 0x%08X, got %p)", what, (unsigned)addr, (unsigned)status, base);
        return -1;
    }
    memset(base, 0, sz);
    xsdk_log_write("mem: %s at 0x%08X: mapped (%u bytes)", what, (unsigned)addr, (unsigned)sz);
    return 0;
#else
    (void)size;
    xsdk_log_write("mem: %s at 0x%08X: fixed mapping not compiled in (build with -DXB_USE_NTALLOC)", what, (unsigned)addr);
    return -1;
#endif
}

/* Allocate the relocatable arena separately from fixed PS2 register windows.
 * This initializer is idempotent and never clears a live game's arena. */
int XbMem_InitHeap(void) {
    if(sHeapRaw != NULL)return 0;
    uint8_t *raw=calloc(1,(size_t)HEAP_SIZE+4096);
    if(raw == NULL)return -1;
    uintptr_t address=((uintptr_t)raw+4095)&~(uintptr_t)4095;
    if(address>UINT32_MAX || (uint64_t)address+HEAP_SIZE>0x100000000ull) {
        free(raw);
        xsdk_log_write("mem: allocated game arena does not fit 32-bit addresses");
        return -1;
    }
    sHeapRaw=raw;sHeapBase=(uint8_t *)address;
    sHeapNext=HEAP_FIRST_OFFSET;sHeapEnd=HEAP_SIZE;
    xsdk_log_write("mem: game arena at %p (%u bytes); fixed register windows separate",(void *)sHeapBase,(unsigned)HEAP_SIZE);
    return 0;
}

unsigned XbMem_Init(void) {
    unsigned failed = 0, i;

    if (sizeof(void *) != 4) {
        xsdk_log_write("mem: pointers are %u bytes; the game needs 4 (wrong ABI?)", (unsigned)sizeof(void *));
        return 0xFFu;
    }
    if (XbMem_InitHeap() != 0) failed |= 1;
    for (i = 0; i < sizeof(kFixed) / sizeof(kFixed[0]); i++) {
        if (fixed_map(kFixed[i].addr, kFixed[i].size, kFixed[i].what) != 0) {
            failed |= kFixed[i].bit;
        }
    }
    gPortScratchpadMapped = (failed & 8) == 0;
    sFailed = failed;
    return failed;
}

/* The game's malloc: a bump allocator over the heap region, 16-byte aligned. The game takes it with ONE malloc at
   start-up (Heap_Create), so this is called once in practice. */
void *Port_Malloc(uint32_t size) {
    uint32_t p = (sHeapNext + 15u) & ~15u;

    if (sHeapBase == NULL || !size || p > sHeapEnd || size > sHeapEnd - p) {
        xsdk_log_write("mem: malloc(0x%X) does not fit in the game heap", (unsigned)size);
        return NULL;
    }
    sHeapNext = p + size;
    return sHeapBase + p;
}

/* Memory the platform hands to the game by address (file handles, the "second processor's" memory): zeroed. The
   Xbox 360's pointers are 4 bytes, so the C library's allocator is enough. (The PC port needs a region of its own
   only because its 64-bit malloc can return addresses above 4 GB; and to make addresses repeat for save states,
   which this build does not have yet.) */
void *Port_LowAlloc(size_t size) { return calloc(1, size); }
void Port_LowFree(void *addr) { free(addr); }

/* A large block for the game that does not come out of its own heap: the buffer of a stage larger than the disc's
   (battle_load.c). As an address, not a pointer: the game's files must not be handed a host function's pointer.
   Blocks are remembered so that Port_GameBigFree can tell them from the game heap's. */
#define BIG_MAX 16
static struct { void *p; unsigned addr,size; } sBig[BIG_MAX];

unsigned Port_GameBigAlloc(int size) {
    void *p;
    int i;

    if (size <= 0) {
        return 0;
    }
    for (i = 0; i < BIG_MAX && sBig[i].p != NULL; i++) {
    }
    if (i == BIG_MAX || (p = calloc(1, (size_t)size)) == NULL) {
        xsdk_log_write("mem: Port_GameBigAlloc(%d) failed", size);
        return 0;
    }
    sBig[i].p = p;
    sBig[i].addr = Port_Addr(p);
    sBig[i].size = (unsigned)size;
    return sBig[i].addr;
}

int Port_GameBigFree(unsigned addr) {
    int i;

    for (i = 0; i < BIG_MAX; i++) {
        if (sBig[i].p != NULL && sBig[i].addr == addr) {
            XbRes_Forget(sBig[i].p,sBig[i].size);
            free(sBig[i].p);
            sBig[i].p = NULL;
            sBig[i].addr = 0;
            sBig[i].size = 0;
            return 1;
        }
    }
    return 0;
}

static int check(int ok, const char *what) {
    xsdk_log_write("mem selftest: %s: %s", what, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

int XbMem_SelfTest(void) {
    int bad = 0;
    unsigned k;

    for (k = 0; k < sizeof(kFixed) / sizeof(kFixed[0]); k++) {
        volatile uint32_t *p = (volatile uint32_t *)Port_Ptr(kFixed[k].addr);
        uint32_t want = 0xA5C30000u + k;
        int ok = 0;

        if ((sFailed & kFixed[k].bit) == 0) { /* never touch a region Init did not get */
            p[0] = want;
            p[1] = ~want;
            ok = p[0] == want && p[1] == ~want;
        }
        bad += check(ok, kFixed[k].what);
    }
    {
        uint32_t *a = Port_Malloc(64), *b = Port_Malloc(64);
        bad += check(a != NULL && b != NULL && b >= a + 16 && ((uintptr_t)a & 15) == 0, "game heap bump allocator");
    }
    {
        uint32_t *h = Port_LowAlloc(32);
        int ok = h != NULL && h[0] == 0 && h[7] == 0;
        if (h != NULL) {
            h[7] = 7;
        }
        Port_LowFree(h);
        bad += check(ok, "Port_LowAlloc zeroed");
    }
    {
        unsigned big = Port_GameBigAlloc(1 << 20);
        int ok = big != 0 && Port_GameBigFree(big) == 1 && Port_GameBigFree(0x003BE730u) == 0;
        bad += check(ok, "Port_GameBigAlloc/Free");
    }
    xsdk_log_write("mem selftest: %d check(s) failed", bad);
    return bad;
}
