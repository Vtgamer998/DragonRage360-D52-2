/* SPDX-License-Identifier: MIT */
#ifndef XB_HEAP_H
#define XB_HEAP_H
/* Status of the actual game Heap_* implementation, not a replacement allocator.
 * These functions are defined by engine/combat/heap.c in the engine build. */
enum {XB_HEAP_OK=0,XB_HEAP_NOT_READY=-510,XB_HEAP_ARGUMENT=-511,
      XB_HEAP_FULL=-512,XB_HEAP_BAD_FREE=-513};
int XbHeap_Ready(void);
int XbHeap_LastError(void);
/* Exact live allocation base only; zero for unknown/freed/interior pointers. */
unsigned XbHeap_AllocationBytes(const void *);
#endif
