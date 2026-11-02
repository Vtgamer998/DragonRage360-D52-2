/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef D50_GFX_PRESENT_H
#define D50_GFX_PRESENT_H
#include <stdint.h>
/* D51 milestone 3: real presentation path.
 *
 * The caller must already have called xsdk_gpu_open and xsdk_present_open
 * (the title's main does both). Renders `frames` frames at
 * logical_width x logical_height into the current front buffer, presents each
 * one, then reads the last frame back from the front buffer.
 *
 * Returns 0 only when every present completed, the GPU waits passed and the
 * readback matched. Distinct log lines separate INIT, PRESENT and VISUAL.
 * The readback proves bytes in the front buffer; it does not prove that the
 * display scanned them out or that a person saw them. */
int D50_GfxPresentRun(uint32_t logical_width,uint32_t logical_height,
                      uint32_t display_width,uint32_t display_height,
                      uint32_t frames);
#endif
