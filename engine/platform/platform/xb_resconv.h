/* SPDX-License-Identifier: MIT
 * Resource endianness for the Xbox 360 build.
 *
 * The files on the disc are PS2 data: little-endian. The game reads them with native loads, and the Xbox 360 is
 * big-endian, so every multi-byte field of every file the game reads has to be converted before the game looks at
 * it. This module is the PLACE where that happens, not the conversions themselves: the formats of most files are
 * not known (docs/systems/graphics.md of the repository: "The model and texture formats are not understood yet").
 * What it does:
 *
 *   1. After each whole-file read from the disc (xb_plat_file.c, gXbFilePostRead) it finds the converter
 *      registered for that file's id and runs it on the buffer.
 *   2. For files the game stores BPE-packed (Bpe_Decode), the converter must run on the UNPACKED bytes, which exist
 *      only inside the game's own code. The packed bytes are endian-neutral except the 8-byte header. So for a
 *      packed file the read only remembers "this buffer is file id N"; src/sys/bpe.c (patch in patches/) reads its
 *      header as little-endian and calls XbRes_AfterBpe(src, out, size) at the end, which runs the converter on the
 *      output.
 *   3. Every file that is read with NO converter registered is logged once with its id: the first run of the game
 *      on the console writes the real list of what still has to be done to default.log (XbRes_Report prints a
 *      summary).
 *
 * File ids are the game's global ids (docs/systems/files_and_assets.md): <= 0 pzs3us0.afs, 1..0xD47 pzs3us1.afs
 * (id = index + 1), >= 0xD48 pzs3us2.afs (id = index + 0xD48).
 */
#ifndef XB_RESCONV_H
#define XB_RESCONV_H
#include <stddef.h>
#include <stdint.h>

/* A converter: `buf` holds the whole file (or, for a packed file, the unpacked bytes), n bytes, in PS2 byte order.
   Rewrite it in place to the host's byte order. */
typedef void (*XbResFn)(void *buf, size_t n, int id);
typedef int (*XbResCheckedFn)(void *buf, size_t n, int id);
/* Locate an exact embedded compressed entry within an already prepared file.
 * Zero means a bounded match; callback and extent must both be supplied. */
typedef int (*XbResEmbeddedFn)(const void *file,size_t file_size,const void *source,
                             size_t *extent,XbResCheckedFn *decoded_converter);
/* Checked converters return zero only after validation and conversion succeed. */
int XbRes_RegisterChecked(int firstId, int lastId, XbResCheckedFn fn, int packed);
typedef struct XbResCheckedRange {int first,last;XbResCheckedFn fn;int packed;} XbResCheckedRange;
/* Entire batch is checked before registration. Refuses active strict reads. */
int XbRes_RegisterCheckedBatch(const XbResCheckedRange *,size_t count);
int XbRes_RegisterContainer(int first,int last,XbResCheckedFn prepare,XbResEmbeddedFn resolve);
typedef struct XbResContainerRange {int first,last;XbResCheckedFn prepare;XbResEmbeddedFn resolve;} XbResContainerRange;
int XbRes_RegisterContainerBatch(const XbResContainerRange *,size_t count);
int XbRes_LastError(void);
/* Transport/queue failure; preserves the first strict-session error. */
void XbRes_SetReadError(int error);
/* Opt-in transport guard for a fresh original boot. Unknown/partial reads,
 * unchecked callbacks and exhausted packed-read tracking are rejected.
 * First failure remains latched until End; this does not install converters
 * or authorize Battle_Main. Begin refuses an active session/pending decodes. */
int XbRes_BeginCheckedReads(void);
int XbRes_CheckedReadsActive(void);
int XbRes_PreflightRead(int id);
void XbRes_EndCheckedReads(void);
int XbRes_HasCheckedConverter(int id);
/* Bytes available from the most recent read containing this pointer; zero
 * means no tracked source. Does not infer bounds from the BPE header. */
size_t XbRes_SourceSize(const void *src);
/* Provenance of a successfully checked, complete raw-file conversion only.
 * No new ownership; invalidated by Forget/End/read-ring replacement. */
int XbRes_ReadFile(const void *inside,const void **base,size_t *size,int *id);
/* Strict reads authorize the format and exact span BEFORE BPE writes output. */
size_t XbRes_BpeSourceSize(const void *src);
void XbRes_Forget(const void *buf, size_t n);

/* Registers fn for the ids firstId..lastId (inclusive). packed = 1: the game BPE-unpacks this file; fn runs on the
   result. Returns 0, or -1 if the table (64 ranges) is full or a range overlaps an earlier one. */
int XbRes_Register(int firstId, int lastId, XbResFn fn, int packed);

/* Hooks the file layer (sets gXbFilePostRead). Call once, before the game starts reading. */
void XbRes_Install(void);

/* Called by Bpe_Decode (patched) with the source it was given, the output and the output size. */
void XbRes_AfterBpe(const void *src, void *out, int32_t size);
/* Explicit destination extent, checked before decompression writes anything. */
void *XbBpe_DecodeBounded(void *,void *,uint32_t,int32_t *);

/* Logs one line per kind: how many files were converted, how many ids were read with no converter. */
void XbRes_Report(void);

/* The global id of a file read from partition ptid at index flid (INT32_MIN for a loose file). */
int XbRes_Id(int ptid, int flid);

/* ---- helpers for writing converters ---- */
static inline uint32_t XbRes_Le32(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
/* Reverse `count` units of 2, 4 or 8 bytes in place. */
void XbRes_Swap16(void *p, size_t count);
void XbRes_Swap32(void *p, size_t count);
void XbRes_Swap64(void *p, size_t count);
/* Swaps buf by a plan: a sequence of items, each a letter and an optional repeat count (default 1):
     B = a byte (not changed), H = u16, I = u32 or float, Q = u64 or double, x = bytes to skip (padding, text).
   A trailing '*' repeats the whole plan to the end of the buffer, e.g. "I2H2B4*" for an array of 16-byte records.
   Returns 0 when the plan consumed the buffer exactly (with '*': a whole number of records), -1 otherwise (the part
   that fits is still converted). */
int XbRes_SwapPlan(void *buf, size_t n, const char *plan);
#endif
