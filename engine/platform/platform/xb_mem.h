/* SPDX-License-Identifier: MIT */
#ifndef XB_MEM_H
#define XB_MEM_H
#include <stddef.h>
#include <stdint.h>

/* Gets the game's memory: its heap (32 MB, relocatable), and the three regions the game's code addresses by
   number: hardware registers 0x10000000 (64 KB), GS registers 0x12000000 (8 KB), scratchpad 0x70000000 (16 KB).
   Returns a bit mask of what FAILED: 1 heap, 2 hardware registers, 4 GS registers, 8 scratchpad. 0 = all there.
   Everything is logged to default.log, which is the point: the fixed mappings are the one thing nobody has seen
   the console accept yet. */
unsigned XbMem_Init(void);
/* Initialize only the relocatable game arena; no MMIO mappings or claim of
 * whole-platform readiness. The allocated arena must fit 32-bit pointers. */
int XbMem_InitHeap(void);
/* Original USA game heap budget, expressed as a SIZE, not a relocated native
 * address subtracted from the PS2's numeric end address. */
#define XB_GAME_HEAP_BYTES ((0x01EFB000u-0x003BE71Cu+3u)&~3u)

/* Writes and reads back a pattern in every region that Init got, and round-trips Port_LowAlloc, Port_Malloc and
   Port_GameBigAlloc. Returns the number of checks that failed (0 = all passed). Safe to call from a diagnostic
   screen. */
int XbMem_SelfTest(void);

void *Port_Malloc(uint32_t size);       /* the game's malloc (bump allocator over the heap) */
void *Port_LowAlloc(size_t size);       /* zeroed, below 4 GB, for what the game is handed by address */
void Port_LowFree(void *addr);
unsigned Port_GameBigAlloc(int size);   /* large block as a game address; 0 = none */
int Port_GameBigFree(unsigned addr);    /* 1 if addr was such a block */

extern int gPortScratchpadMapped;       /* 1 if the scratchpad is at 0x70000000 */
#endif
