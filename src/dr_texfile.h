/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef DR_TEXFILE_H
#define DR_TEXFILE_H
#include "dr_disc.h"
#include <stddef.h>
#include <stdint.h>
typedef struct {uint32_t width,height,psm;size_t rgba_bytes;} DrTexFileImage;
/* Bounded TexFile subset: CT32 upload packets, CT32/PSMT8/PSMT4 base level,
 * CSM1 with CT32 palettes and CSA=0. Validates coverage before writing RGBA.
 * No relocation, mipmaps, GS state emulation or mapping to body materials. */
/* Transfer bounds/schema only; indexed pixel-only entries may use an external
 * CLUT. Own palette and full RGBA coverage are required by decode. */
int dr_texfile_validate_transfers(const void *,size_t,uint32_t);
int dr_texfile_validate_palette(const void *,size_t,uint32_t);
/* Validate the whole TexFile before rewriting CPU scalars in place. Relative
 * offsets remain for original Res_RelocateOffsets; upload packets stay LE.
 * Pointer slots are cleared. Refusal leaves the input unchanged. */
int dr_texfile_prepare(void *,size_t,int big_endian);
/* FontData uses byte offsets and a four-CLUT font palette. */
int dr_font_prepare(void *,size_t,int big_endian);
int dr_texfile_decode(const void *,size_t,uint32_t index,void *rgba,size_t capacity,DrTexFileImage *);
int dr_texfile_model_image(const dr_disc *,const char *pack_path,uint32_t index,
                          void *rgba,size_t capacity,DrTexFileImage *);
/* Match the relative TEX0 emitted by a model to one unique entry, including
 * TBP/CBP offsets; ignore only TCC/TFX and CLD, which are draw/cache state. */
int dr_texfile_material(const void *,size_t,uint64_t tex0,uint32_t *index);
int dr_texfile_body_material(const dr_disc *,const char *pack_path,uint64_t tex0,
                            void *rgba,size_t capacity,DrTexFileImage *,uint32_t *index);
#endif
