/* SPDX-License-Identifier: MIT
 * Game addresses in the Xbox 360 port's own code (counterpart of port/src/port_addr.h in the PC port).
 *
 * The game keeps addresses in 4 bytes. The Xbox 360 user-mode ABI already has 32-bit effective addresses, so by
 * default a game address IS the host address (plain casts), as on 32-bit Linux. If the console refuses the fixed
 * mappings that the game's numeric addresses need (see xb_plat_mem.c, XbMem_Init), build with -DXB_ADDR_WINDOW:
 * a game address is then an offset into one window, as on macOS in the PC port. NOTE: with a window, game code
 * that dereferences a numeric PS2 address (hardware registers, scratchpad) must be rewritten too; that rewrite
 * does not exist yet (TODO-VERIFICAR: only needed if fixed mapping fails).
 */
#ifndef XB_PORT_ADDR_H
#define XB_PORT_ADDR_H
#include <stdint.h>

#ifdef XB_ADDR_WINDOW
extern uintptr_t gXbWindow; /* host address of game address 0 (xb_plat_mem.c) */
static inline void *Port_Ptr(uint32_t addr) { return addr != 0 ? (void *)(gXbWindow + addr) : (void *)0; }
static inline uint32_t Port_Addr(const volatile void *p) { return p != 0 ? (uint32_t)((uintptr_t)p - gXbWindow) : 0; }
#else
#define Port_Ptr(addr) ((void *)(uintptr_t)(uint32_t)(addr))
#define Port_Addr(p) ((uint32_t)(uintptr_t)(p))
#endif
#endif
