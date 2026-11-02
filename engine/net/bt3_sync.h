/* SPDX-License-Identifier: MIT */
#ifndef BT3_SYNC_H
#define BT3_SYNC_H
#include <stdint.h>
#include <stddef.h>
/* Semantic fields hashed by upstream headless.c Port_NetDigest, in that order.
 * Position components are IEEE-754 bit patterns, never float conversions.
 * Explicit fields avoid host endianness, alignment and structure padding.
 */
typedef struct {
    uint32_t arrived;
    int32_t mode,first,battle,sequence,clock,hp[2];
    uint32_t position_bits[2][3];
} Bt3SyncState;
enum { BT3_SYNC_BYTES=56 };
int bt3_sync_bytes(uint8_t out[BT3_SYNC_BYTES],const Bt3SyncState *state);
int bt3_sync_digest(const Bt3SyncState *state,uint64_t *digest);
#endif
