/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef DR_DMA_PRODUCER_H
#define DR_DMA_PRODUCER_H
#include "dr_gs_stream.h"
enum { DR_DMA_INVALID=-420, DR_DMA_STATE=-421, DR_DMA_FULL=-422, DR_DMA_MEMORY=-423 };
/* Adapted subset of upstream src/sys/dma.c, revision 3ca419c. Single owner,
 * two 1 MiB buffers. A submit callback must consume all bytes synchronously;
 * GPU submission/present belongs to the caller. No native pointer is stored
 * as a game address. Private chain addresses are 0x41000000/0x41100000.
 * This is NOT a replacement for the absent original gDmaCur/heap platform.
 * Dma_AddNext requires the explicit mapped-submit API below. */
typedef int (*DrDmaSubmit)(void *,const DrGsSpan *,uint32_t);
typedef int (*DrDmaSubmitMapped)(void *,const DrGsSpan *,size_t,uint32_t);
/* Optional explicit mappings, borrowed until synchronous Flush returns.
 * Up to 16 maps / 8 MiB; canonical snapshots do not alter the input buffers.
 * Packet writers MUST obtain tag addresses through chain_address. Existing
 * pointer-cast VU1 packet writers have not been rewritten automatically. */
int dr_dma_producer_open_mapped(DrDmaSubmitMapped,void *);
int dr_dma_producer_open(DrDmaSubmit,void *);
int dr_dma_producer_close(void);
int dr_dma_producer_error(void);
int dr_dma_producer_is_open(void);
int dr_dma_producer_view(DrGsSpan *); /* read-only queued bytes, before FINISH/END */
/* Explicit input types. Canonical data already contains complete LE DMA
 * quadwords. Native numeric u32 arrays are serialized word by word. Neither
 * operation guesses the format of a mixed engine buffer. */
int dr_dma_append_canonical(const void *,size_t);
/* Only the 48-byte native TexBltPacket, not an arbitrary mixed buffer. */
int dr_dma_add_texture_header(const void *);
int dr_dma_append_words(const uint32_t *,size_t words);
int dr_dma_write_ad(uint32_t address,unsigned long long value);
/* Producers whose callers fill memory themselves (0.25).
 * The engine writes u32/u64 values with native stores. On a big-endian host
 * those bytes are the opposite of what the PS2 DMA/VIF/GIF stream needs, so
 * every region records HOW it was written and is converted to canonical LE
 * once, in Dma_Flush (or dr_dma_producer_view), never twice.
 *   RAW: bytes are already canonical LE (file data, microprogram code).
 *   W32: the caller stored u32/f32 words natively (VU1 packets, pkt[] arrays).
 *   W64: the caller stored u64 values natively (GIF tags, A+D pairs, XYZ/UV).
 * A region mixing u32 and u64 stores cannot be converted correctly; the
 * caller must be split or rewritten. Defaults: Alloc=W32, BeginDirect=W64,
 * AddData=W32, AddRef=RAW. Use the *_kind variants to override. */
typedef enum { DR_DMA_RAW=0, DR_DMA_W32=1, DR_DMA_W64=2 } DrDmaKind;
int dr_dma_add_clut_ref(const void *);
int dr_dma_add_xfade_ref(const void *,const void *,size_t);
int dr_dma_snapshot_chain(const void *,size_t,uint32_t *);
int dr_dma_snapshot_words(const void *,size_t,uint32_t *);
int dr_dma_map_chain(void *,size_t,DrDmaKind,uint32_t *address);
int dr_dma_chain_address(const void *,uint32_t *address);
void Dma_AddNext(void *addr,uint32_t *retTag);
void dr_dma_native_swap(int mode); /* -1 auto (host endianness), 0 never, 1 always (tests) */
void *dr_dma_alloc_kind(int32_t size,DrDmaKind kind);
void dr_dma_add_data_kind(const void *src,int32_t size,DrDmaKind kind);
void dr_dma_add_ref_kind(const void *addr,int32_t size,DrDmaKind kind);
int dr_dma_w64_range(void *,size_t); /* bounds check, no writes or conversion */
int dr_dma_reject_packet(void); /* sticky invalid-packet error */
int dr_dma_pending_regions(void); /* regions awaiting conversion */
/* Original signatures retained only for the producers adapted here. */
void Port_GsMarker(int effect);
void Dma_Flush(void);
void Dma_ResetBuffers(void); /* discards queued bytes, preserves a sticky error */
int32_t Dma_GetBufSize(void);
int32_t Dma_GetUsedSize(void);
int32_t Dma_GetFreeSize(void);
void Dma_AddTexFlush(void);
void Dma_AddFrame(int32_t,int32_t,int32_t);
void Dma_AddClamp(int32_t,int32_t,int32_t,int32_t,int32_t,int32_t);
void Dma_AddScissor(int32_t,int32_t,int32_t,int32_t);
void Dma_AddXyOffset(int32_t,int32_t);
void Dma_AddZbuf(int32_t,int32_t);
void Dma_AddTex0(unsigned long long);
void Dma_AddTex1(unsigned long long);
void Dma_AddFba(int32_t);
void Dma_AddColClamp(unsigned long long);
void Dma_AddAlpha(unsigned long long);
void Dma_AddFillRect(int32_t,int32_t,int32_t,int32_t,uint32_t);
void *Dma_Alloc(int32_t size);                  /* floors to 16; default W32 */
unsigned long long *Dma_BeginDirect(void);      /* 16-byte aligned; room for 64 KiB */
void Dma_EndDirect(unsigned long long *end);    /* writes CNT + VIF FLUSHE/DIRECT */
void Dma_AddData(void *src,int32_t size);       /* copies floor(size/16)*16 bytes */
/* Mapped submissions use REF tags into a separate canonical snapshot arena
 * (8 MiB maximum per Flush). The command buffers remain 1 MiB each.
 * The single-span API retains its historical inline CNT copy. Both snapshot
 * at call time and floor the size to 16; no source pointer enters a tag. */
void Dma_AddRef(void *addr,int32_t size);
#endif
