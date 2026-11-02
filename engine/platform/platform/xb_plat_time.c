/* SPDX-License-Identifier: MIT
 * Vertical blank pacing, as the PC port does it in port/src/plat_stub.c (vblank_wait, Port_VBlank), without what
 * only the PC has: no window, no settings (game speed fixed at 100%), no "low input lag", no online session, no
 * pause overlay, no pad recording. The grid logic is the PC's: one blank every 1/59.94 s; a blank that is late but
 * not hopelessly keeps the grid (so a fight frame of two blanks still lasts 33.4 ms), a blank more than 0.1 s
 * behind starts a new grid.
 *
 * TODO-VERIFICAR: 49.875 MHz is the Xenon time base as I know it; check against the console (the pace line in
 * default.log, written once a second with -DXB_PACE_LOG, should read 16.6x ms apart).
 * TODO-VERIFICAR: NtDelayExecution(alertable, interval) as declared by xsdk/kernel.h, only used with
 * -DXB_USE_NTDELAY. Without it the wait spins on the time base: correct but it keeps a core busy.
 */
#include <stdint.h>
#include <stdio.h>
#include "xb_time.h"
#ifdef XB_USE_NTDELAY
#include "xsdk/kernel.h"
#endif
#if defined(XB_PACE_LOG)
#include "xsdk/log.h"
#endif
#if !defined(__powerpc__) && !defined(__PPC__)
#include <time.h> /* host builds (syntax and logic tests) */
#endif

#define TB_HZ 49875000ull
#define BLANK_NS 16683350ull

unsigned gPortVBlanks, gPortLiveBlanks;
unsigned long long gPortSleptNs;

uint64_t XbTime_NowNs(void) {
#if defined(__powerpc__) || defined(__PPC__)
    uint64_t t;
#if defined(__powerpc64__) || defined(__LP64__)
    __asm__ volatile("mftb %0" : "=r"(t));
#else
    uint32_t hi, lo, hi2;
    do {
        __asm__ volatile("mftbu %0" : "=r"(hi));
        __asm__ volatile("mftb %0" : "=r"(lo));
        __asm__ volatile("mftbu %0" : "=r"(hi2));
    } while (hi != hi2);
    t = ((uint64_t)hi << 32) | lo;
#endif
    /* t * 1e9 / TB_HZ without overflowing 64 bits: whole seconds apart from the remainder */
    return (t / TB_HZ) * 1000000000ull + (t % TB_HZ) * 1000000000ull / TB_HZ;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

void XbTime_WaitUntilNs(uint64_t when) {
#ifdef XB_USE_NTDELAY
    uint64_t t = XbTime_NowNs();
    if (when > t && when - t > 2500000ull) { /* sleep to within 2 ms, spin the rest (the kernel's timer is coarse) */
        int64_t rel = -(int64_t)((when - t - 2000000ull) / 100ull); /* negative = relative, in 100 ns */
        (void)KeDelayExecutionThread(1 /* USER_MODE */, 0, &rel);
    }
#endif
    while (XbTime_NowNs() < when) {
    }
}

static void (*sHooks[8])(void);
static int sHookCount;
int XbTime_OnVBlank(void (*fn)(void)) {
    if (fn == 0 || sHookCount >= 8) {
        return -1;
    }
    sHooks[sHookCount++] = fn;
    return 0;
}

static int (*sVsyncHandler)(int);
void *sceGsSyncVCallback(int (*handler)(int)) {
    void *old = (void *)sVsyncHandler;
    sVsyncHandler = handler;
    return old;
}

static uint64_t sNext; /* when the coming blank is due */
static uint64_t sPrev, sMin = ~0ull, sMax;
static unsigned sCount, sLate, sReset;

unsigned long long Port_BlankNs(void) { return BLANK_NS; }
int Port_SpeedMilli(void) { return 1000; }

int Port_VBlankUsLeft(void) {
    uint64_t t = XbTime_NowNs();
    return sNext > t && sNext - t < 100000000ull ? (int)((sNext - t) / 1000ull) : 0;
}
int Port_PaceBehindUs(void) {
    uint64_t t = XbTime_NowNs();
    return sNext != 0 && t > sNext && t - sNext < 100000000ull ? (int)((t - sNext) / 1000ull) : 0;
}

static void vblank_wait(void) {
    uint64_t t = XbTime_NowNs(), after;

    if (sNext != 0 && t >= sNext && t - sNext < 100000000ull) {
        after = t; /* late but not hopelessly: do not wait, keep the grid */
        sNext += BLANK_NS;
        sLate++;
    } else if (sNext > t && sNext - t < 100000000ull) {
        XbTime_WaitUntilNs(sNext);
        after = XbTime_NowNs();
        gPortSleptNs += after - t;
        sNext += BLANK_NS;
    } else {
        after = t; /* the first one, or far behind: a new grid from now */
        sNext = t + BLANK_NS;
        sReset++;
    }
    if (sPrev != 0) {
        uint64_t d = after - sPrev;
        if (d < sMin) { sMin = d; }
        if (d > sMax) { sMax = d; }
    }
    sPrev = after;
    if (++sCount == 60) {
#ifdef XB_PACE_LOG
        xsdk_log_write("pace: 60 blanks: %u.%02u to %u.%02u ms apart, %u late, %u new grids", (unsigned)(sMin / 1000000ull),
                       (unsigned)(sMin / 10000ull % 100), (unsigned)(sMax / 1000000ull), (unsigned)(sMax / 10000ull % 100), sLate, sReset);
#endif
        sCount = sLate = sReset = 0;
        sMin = ~0ull;
        sMax = 0;
    }
}

void Port_VBlank(void) {
    int i;

    vblank_wait();
    gPortVBlanks++;
    gPortLiveBlanks++;
    for (i = 0; i < sHookCount; i++) {
        sHooks[i]();
    }
    if (sVsyncHandler != 0) {
        sVsyncHandler(0);
    }
}
int sceGsSyncV() { Port_VBlank(); return 0; }
