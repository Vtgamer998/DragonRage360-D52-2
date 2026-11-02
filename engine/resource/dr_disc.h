/* SPDX-License-Identifier: MIT */
#ifndef DR_DISC_H
#define DR_DISC_H
#include <stddef.h>
#include <stdint.h>
/* Return 0 only for an exact read. Callback must support 64-bit offsets.
 * Caller owns the device/file and serializes concurrent callback access. */
typedef int (*dr_read_at)(void *, uint64_t, void *, size_t);
typedef struct { uint64_t offset, size; unsigned directory; } dr_span;
typedef struct {
    dr_read_at read; void *opaque; uint64_t size; dr_span root;
} dr_disc;
enum { DR_OK=0, DR_IO=-1, DR_FORMAT=-2, DR_RANGE=-3,
       DR_NOT_FOUND=-4, DR_UNSUPPORTED=-5 };
int dr_mount(dr_disc *, dr_read_at, void *, uint64_t);
int dr_find(const dr_disc *, const char *, dr_span *);
int dr_read(const dr_disc *, const dr_span *, uint64_t, void *, size_t);
int dr_afs_entry(const dr_disc *, const dr_span *, uint32_t, dr_span *);
/* Maps upstream disc/.. and pzs3usN/NNNNN.bin paths without extracting ISO.
 * Does not implement mods, saves, fingerprinting, or host FILE* adapters. */
int dr_game_file(const dr_disc *, const char *, dr_span *);
const char *dr_error(int);
#endif
