/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef D50_GFX_PROBE_H
#define D50_GFX_PROBE_H
#include <stdint.h>
/* D50 milestone 2: background fill plus one triangle through dr_xenos, no game data.
 * Returns a dr_xenos status code (0 = DR_XENOS_OK). The caller must have
 * opened nothing else: the probe owns dr_xenos_open/close. */
int D50_GfxProbeRun(uint32_t width,uint32_t height);
/* Scene shared with the D51 present path. Draws inside an active batch, in
 * logical pixel coordinates (not clip space). */
int D50_GfxSceneDraw(uint32_t width,uint32_t height,uint32_t texture);
typedef struct {
    uint32_t background_argb,triangle_argb;
    uint32_t center_x,center_y; /* inside the triangle */
    uint32_t corner_x,corner_y; /* background only */
} D50GfxExpectation;
D50GfxExpectation D50_GfxExpectation(uint32_t width,uint32_t height);
#endif
