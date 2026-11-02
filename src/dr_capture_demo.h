/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef DR_CAPTURE_DEMO_H
#define DR_CAPTURE_DEMO_H
#include <stdint.h>
int dr_capture_demo_open(uint32_t width,uint32_t height);
int dr_capture_demo_draw(void);
int dr_capture_demo_close(void);
int dr_capture_original_open(uint32_t width,uint32_t height);
int dr_capture_original_submit(void);
/* Diagnostic CPU producer/decode pass only; no GPU replay or presentation. */
int dr_capture_original_decode(void);
typedef struct {uint64_t dma_us,replay_us;uint32_t draws,vertices,vu_vertices,culled_strips;} DrCaptureProfile;
void dr_capture_original_profile(DrCaptureProfile *);
#endif
