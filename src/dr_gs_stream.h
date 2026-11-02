/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef DR_GS_STREAM_H
#define DR_GS_STREAM_H
#include <stddef.h>
#include <stdint.h>
enum { DR_STREAM_INVALID=-400, DR_STREAM_RANGE=-401, DR_STREAM_TRUNCATED=-402,
       DR_STREAM_BUDGET=-403, DR_STREAM_COMMAND=-404, DR_STREAM_STACK=-405 };
typedef int (*DrStreamGif)(void *,const unsigned char *,uint32_t);
/* Explicit game-address mappings. Tags, VIF words, microcode, GIF and UNPACK
 * payloads must ALL be canonical little-endian. Native mixed u32/u64 packet
 * builders must be adapted separately; this is not a whole-buffer byteswap.
 * Mappings are borrowed/immutable during execution and may not overlap. */
typedef struct { uint32_t address; const void *bytes; size_t size; } DrGsSpan;
typedef struct {
    uint32_t tags,words,gif_quadwords,vu_calls,fail_address,fail_command;
} DrGsStreamStats;
typedef struct {
    DrStreamGif gif;void *opaque;
    uint32_t cl,wl,pending,need,have,direct,direct_have;
    uint32_t tag_budget,word_budget,vu_budget;
    unsigned char payload[4096],quad[16];
    int error;
    DrGsStreamStats stats;
} DrGsStream;
/* One active VU interpreter globally, single thread. Init resets VU state;
 * successive chains preserve programs, VU memory and VIF control registers.
 * Stats and tag/word limits apply per chain (per init for standalone VIF).
 * Not a simulation-state or rollback interface. */
int dr_gs_stream_init(DrGsStream *,DrStreamGif,void *);
/* Accepts partial runs, e.g. a VIF command in TTE followed by REF payload. */
int dr_gs_stream_vif(DrGsStream *,const void *,size_t);
int dr_gs_stream_finish(DrGsStream *);
/* Bounded DMA source-chain interpreter: REFE/CNT/NEXT/REF/REFS/CALL/RET/END.
 * Error is sticky until init. A failed chain can modify VU/GS/VRAM state;
 * discard its captured draws and reinitialize before another attempt.
 * No GPU work/present is submitted by this module. */
/* GIF-channel transfers share the checked chain walker; tag TTE words are
 * never interpreted as VIF on this channel. No VU state is reset. */
int dr_gs_stream_gif(DrGsStream *,const void *,size_t);
int dr_gs_stream_gif_chain(DrGsStream *,const DrGsSpan *,size_t,uint32_t);
int dr_gs_stream_chain(DrGsStream *,const DrGsSpan *,size_t,uint32_t,int tte);
#endif
