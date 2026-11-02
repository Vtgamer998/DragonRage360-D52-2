/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef DR_GRAPHICS_DEMO_H
#define DR_GRAPHICS_DEMO_H
#include <stdint.h>
int dr_graphics_demo_open(void);
int dr_graphics_demo_draw(uint32_t width,uint32_t height);
int dr_graphics_demo_close(void);
#endif
