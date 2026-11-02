/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef DR_GS_TEXTURE_H
#define DR_GS_TEXTURE_H
#include <stddef.h>
#include <stdint.h>
enum { DR_GS_OK=0,DR_GS_INVALID=-1,DR_GS_UNSUPPORTED=-2,DR_GS_CAPACITY=-3 };
typedef struct {
    uint32_t width,height,tbp,tbw,psm,cbp,cpsm,csm;
    uint32_t ta0,ta1,aem;
} DrGsTexture;
/* GS VRAM bytes are canonical little endian, NOT a native uint32 array.
 * Pointers count 256-byte blocks; widths count 64 pixels. Wraps at 4 MiB.
 * Address reports a 32-bit containing word and the field within it. */
int dr_gs_address(uint32_t bp,uint32_t bw,uint32_t psm,uint32_t x,uint32_t y,
                  size_t *word_byte,unsigned *shift,unsigned *bits);
int dr_gs_raw_read(const void *,size_t,uint32_t bp,uint32_t bw,uint32_t psm,
                   uint32_t x,uint32_t y,uint32_t *value);
/* CSM1 indexed palettes only. Base level, no mipmaps. Output RGBA bytes keeps
 * GS alpha (0x80 opaque); do not double-normalize before the modulate shader.
 * Unsupported modes fail before writing output. Buffers must not overlap. */
int dr_gs_texture_decode(const void *,size_t,const DrGsTexture *,void *rgba,size_t capacity);
int dr_gs_texture_selftest(void);
#endif
