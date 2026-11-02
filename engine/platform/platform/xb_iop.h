/* SPDX-License-Identifier: MIT */
#ifndef XB_IOP_H
#define XB_IOP_H
#include <stdint.h>
#include <stddef.h>
/* Owned host-memory stand-in for IOP buffers. Does not execute an IOP, RPC,
 * SPU2 or audio decoder. Interior destinations are checked against size. */
void *sceSifAllocIopHeap(int size);
int sceSifFreeIopHeap(void *addr);
int XbIop_Copy(void *dst,const void *src,int32_t size);
int XbIop_LastError(void);
/* Read-only view of an owned allocation, including interior pointers. */
const void *XbIop_View(uintptr_t,size_t *available);
void Dma_SendToIop(void *dst,void *src,int32_t size);
#endif
