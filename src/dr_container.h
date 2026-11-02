/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef DR_CONTAINER_H
#define DR_CONTAINER_H
#include "dr_disc.h"
#include "dr_resource.h"
/* Strict character pack: word 0=count, count+1 offsets; low 2 bits are flags.
 * Public index is zero-based; original BObjFile_GetEntry uses index+1.
 * Rejects non-monotonic/modded offset layouts. No native pointers on disk. */
int dr_pack_entry(const dr_disc *,const dr_span *,uint32_t,dr_span *,unsigned *);
int dr_pack_memory_entry(const void *,size_t,uint32_t,size_t *,size_t *,unsigned *);
int dr_pack_memory_span(const void *,size_t,const void *entry,size_t *length);
int dr_motion_from_memory(const void *,size_t,int compressed,int big_endian,void *out,size_t capacity,size_t *written);
/* Caller supplies compressed scratch and decoded destination; no heap allocation.
 * compressed=0 copies an uncompressed embedded animation before conversion.
 * Returns disc errors or DR_LOAD_DECODE / DR_LOAD_ANIMATION. Outputs reset on failure.
 * Input/scratch/output buffers must not overlap. */
/* Decoded zero-length BPE slots are absent motions, never playable objects. */
enum { DR_LOAD_DECODE=-20,DR_LOAD_ANIMATION=-21,DR_LOAD_EMPTY=-22 };
int dr_load_motion(const dr_disc *,const dr_span *,int compressed,int big_endian,
                   void *scratch,size_t scratch_cap,void *out,size_t out_cap,size_t *written);
int dr_container_selftest(void);
#endif
