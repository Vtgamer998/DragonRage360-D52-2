/* SPDX-License-Identifier: MIT
 * Time, vertical blank and random numbers of the Xbox 360 build (counterpart of the time part of
 * port/src/plat_stub.c and of port/src/plat_sys.c of the PC port). */
#ifndef XB_TIME_H
#define XB_TIME_H
#include <stdint.h>

/* Monotonic clock in nanoseconds (the PowerPC time base, 49.875 MHz on the Xenon). */
uint64_t XbTime_NowNs(void);
/* Waits until XbTime_NowNs() reaches `when`. Sleeps through the kernel if built with -DXB_USE_NTDELAY, else spins. */
void XbTime_WaitUntilNs(uint64_t when);

/* Things the PC's Port_VBlank does besides pacing (sound tick, panels, online, trace, ...) are not part of this
   module: they register here and run at every blank, in registration order, before the game's own handler.
   Up to 8. Returns 0 if registered. */
int XbTime_OnVBlank(void (*fn)(void));

extern unsigned gPortVBlanks;   /* blanks since start (the game's clock) */
extern unsigned gPortLiveBlanks;
extern unsigned long long gPortSleptNs;

/* The game's own functions, same names and meaning as the PC port:
   Port_VBlank(void)            waits for the coming blank (16.68 ms grid), counts it, runs the hooks and the
                                handler given to sceGsSyncVCallback
   Port_BlankNs(void)           length of a blank in ns
   Port_VBlankUsLeft(void), Port_PaceBehindUs(void), Port_SpeedMilli(void) (always 1000 here) */
#endif
