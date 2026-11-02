/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Dma_PutTexStrips adapted from pinned upstream src/sys/dma.c (3ca419c).
 * Compile through the PS2 arithmetic pass, hard ABI; NOT ordinary native FP.
 * Native u64 stores are canonicalized by the DMA producer once at submission.
 * The fractional branch deliberately preserves upstream's unshifted x0.
 */
#include "common.h"
#include "sys/dma.h"
#include <stddef.h>
extern int dr_dma_w64_range(void *,size_t);
extern int dr_dma_reject_packet(void);
void Dma_PutTexStrips(GsQword **pp, s32 x0, s32 y0, s32 x1, s32 y1, s32 xFrac, s32 yFrac, s32 u0, s32 v0, s32 u1, s32 v1,
                      s32 uFrac, s32 vFrac, u32 rgba, s32 blend) {
    s32 step = 32;
    s32 n;
    s32 w;
    s32 i;
    /* Reject malformed ranges before division, overflow, or packet writes.
     * Supported coordinates fit the GS unsigned 16-bit subpixel fields.
     * Negative screen coordinates down to -0x700 are representable because
     * the original adds the GS origin. C shifts of negative signed values in
     * the original are replaced below by exact, bounded multiplication. */
    if (!pp || !*pp || x0 < -0x700 || x0 > 0x8FF || x1 < x0 || x1 > 0x8FF ||
        y0 < -0x720 || y0 > 0x8DF || y1 < y0 || y1 > 0x8DF ||
        u0 < 0 || u0 > 4095 || u1 <= u0 || u1 > 4095 ||
        v0 < 0 || v0 > 4095 || v1 < v0 || v1 > 4095 ||
        xFrac < 0 || xFrac > 15 || yFrac < 0 || yFrac > 15 ||
        uFrac < 0 || uFrac > 15 || vFrac < 0 || vFrac > 15) {
        dr_dma_reject_packet();return;
    }
    n = (u1 - u0) / step;
    w = x1 - x0;
    if (n < 1 || dr_dma_w64_range(*pp, (size_t)(4 + 2 * n) * 16)) {
        dr_dma_reject_packet();return;
    }

    (*pp)->d[0] = GIF_TAG(2, 0, 1);
    (*pp)->d[1] = GIF_REG_AD;
    (*pp)++;
    (*pp)->d[0] = blend ? 0x156 : 0x116;
    (*pp)->d[1] = GS_PRIM;
    (*pp)++;
    (*pp)->d[0] = (u64)rgba | (0x3F800000ULL << 32);
    (*pp)->d[1] = GS_RGBAQ;
    (*pp)++;
    (*pp)->d[0] = GIF_TAG_EX(n, 1, GIF_FLG_REGLIST, 4);
    (*pp)->d[1] = 0x5353;
    (*pp)++;
    if (w % n == 0) {
        s32 dx = w / n;

        for (i = 0; i < n; i++) {
            (*pp)->d[0] = GS_SET_UV(((u0 + i * step) << 4) + uFrac, (v0 << 4) + vFrac);
            (*pp)->d[1] = GS_SET_XYZ(((x0 + i * dx) * 16) + xFrac + 0x7000, ((y0 + 0x720) << 4) + yFrac, 0);
            (*pp)++;
            (*pp)->d[0] = GS_SET_UV(((u0 + (i + 1) * step) << 4) + uFrac, (v1 << 4) + vFrac);
            (*pp)->d[1] = GS_SET_XYZ(((x0 + (i + 1) * dx) * 16) + xFrac + 0x7000, ((y1 + 0x720) << 4) + yFrac, 0);
            (*pp)++;
        }
    } else {
        f32 dx = (f32)(w << 4) / (f32)n;

        for (i = 0; i < n; i++) {
            (*pp)->d[0] = GS_SET_UV(((u0 + i * step) << 4) + uFrac, (v0 << 4) + vFrac);
            (*pp)->d[1] = GS_SET_XYZ(x0 + (xFrac + (s32)(dx * (f32)i)) + 0x7000, ((y0 + 0x720) << 4) + yFrac, 0);
            (*pp)++;
            (*pp)->d[0] = GS_SET_UV(((u0 + (i + 1) * step) << 4) + uFrac, (v1 << 4) + vFrac);
            (*pp)->d[1] = GS_SET_XYZ(x0 + (xFrac + (s32)(dx * (f32)(i + 1))) + 0x7000, ((y1 + 0x720) << 4) + yFrac, 0);
            (*pp)++;
        }
    }
}
