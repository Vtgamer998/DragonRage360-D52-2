/* SPDX-License-Identifier: MIT
 * Sony kernel, SIF and CD calls of the game: counterpart of port/src/plat_sys.c of the PC port (same answers),
 * plus the IOP heap and RPC stand-ins that the PC keeps in plat_stub.c. Port_Rand is the PS2 executable's own
 * generator (newlib's 64-bit one) and is part of the simulation state: it must be bit-exact, so nothing here is
 * simplified. */
#include <stddef.h>
#include <stdint.h>
#include "xb_port_addr.h"

extern void *Port_LowAlloc(size_t size);
extern void Port_LowFree(void *addr);

int GetThreadId(void) { return 1; }
int ChangeThreadPriority(int tid, int prio) { (void)tid; (void)prio; return 0; }
/* Native GS uploads maintain the cache of each GPU buffer explicitly. There
 * is no PS2 IOP DMA engine here; order CPU stores and overlay instruction
 * visibility. This is not emulation of a PS2 global cache sweep. */
void FlushCache(int mode) {
    __sync_synchronize();
#if defined(__powerpc__) || defined(__PPC__)
    if(mode==2)__asm__ volatile("isync" ::: "memory");
#else
    (void)mode;
#endif
}

void sceSifInitRpc(uint32_t mode) { (void)mode; }
int sceSifRebootIop(char *img) { (void)img; return 1; }
int sceSifSyncIop(void) { return 1; }
int sceSifLoadModule(char *name, int argLen, char *args) { (void)name; (void)argLen; (void)args; return 0; }
int sceFsReset(void) { return 0; }
int sceCdInit(int mode) { (void)mode; return 1; }
int sceCdMmode(int media) { (void)media; return 1; }

int sceSifInitIopHeap() { return 0; }
int sceSifQueryTotalFreeMemSize() { return 0x100000; }
int sceSifQueryMaxFreeMemSize() { return 0x100000; }
/* bind answered at once: sceSifClientData.serve (offset 0x24) becomes non-NULL. A 4-byte field of the game. */
int sceSifBindRpc(void *client, int id, int mode) {
    extern int XbSfx_Bind(void *,int,int);
    return XbSfx_Bind(client,id,mode);
}
int func_002B4AF0() { return 0; } /* sceSifCheckStatRpc: never busy */
int sceSifDmaStat() { return -1; }
int sceSdRemoteInit() { return 0; }
int func_00296B48() { return 0; }

/* Seed of the game's libc rand at start-up. Fixed so a run repeats (the PC's default); XbSys_SetSeed overrides. */
static long sSeed = 0x12345678;
void XbSys_SetSeed(long seed) { sSeed = seed; }
long func_002BB390(void) { return sSeed; }

unsigned long long gPortRandNext = 1;
void Port_Srand(unsigned seed) { gPortRandNext = seed; }
int Port_Rand(void) {
    gPortRandNext = gPortRandNext * 6364136223846793005ull + 1;
    return (int)((gPortRandNext >> 32) & 0x7FFFFFFF);
}
