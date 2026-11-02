/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef DR_VU1_H
#define DR_VU1_H
#include <stddef.h>
#include <stdint.h>
enum {DR_VU_OK=0,DR_VU_INVALID=-200,DR_VU_TRUNCATED=-201,DR_VU_BUDGET=-202,DR_VU_UNKNOWN=-203};
typedef void (*DrVuGif)(void *opaque,const unsigned char *canonical_le,uint32_t quadwords);
/* One reference VU interpreter. Graphics, not deterministic combat/rollback.
 * Memory/program buffers remain canonical LE. Call from one thread at a time.
 * Uses original upstream interpreter arithmetic, not full PS2 timing emulation.
 * FTOI conversions use defined integer decode with sign saturation, including exponent 255. */
void dr_vu1_reset(DrVuGif,void *opaque);
int dr_vu1_control(uint32_t cl,uint32_t wl,uint32_t mode,uint32_t mask,
                   const void *row16,const void *col16);
int dr_vu1_address(uint32_t base,uint32_t offset,uint32_t itop);
/* Individual VIF control writes; unlike dr_vu1_address, BASE/ITOP must not
 * reset OFFSET's double-buffer state. Payload is canonical LE. */
int dr_vu1_vif_control(uint32_t code,const void *payload,size_t bytes);
int dr_vu1_program(uint32_t instruction_address,const void *bytes,size_t size);
int dr_vu1_unpack(uint32_t cmd,uint32_t num,uint32_t immediate,const void *bytes,size_t size);
int dr_vu1_call(int entry,uint32_t instruction_budget);
int dr_vu1_read(uint32_t byte_offset,void *out,size_t bytes);
int dr_vu1_selftest(void);
typedef int (*DrVuStageRenderer)(void *,const uint8_t *,uint32_t,const float *,uint32_t,const float *);
void dr_vu1_stage_renderer(DrVuStageRenderer,void *);
void dr_vu1_profile(uint32_t *,uint32_t *,uint32_t *);
typedef int (*DrVuModelRenderer)(void *,const uint8_t *,uint32_t,const float *,uint32_t,const float *,unsigned);
void dr_vu1_model_renderer(DrVuModelRenderer,void *);
void dr_vu1_model_profile(uint32_t *,uint32_t *);
typedef struct {uint32_t active,program,pc,kick,top,q_bits;} DrVuTrace;
void dr_vu1_trace(DrVuTrace *);
#endif
