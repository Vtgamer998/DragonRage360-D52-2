/* SPDX-License-Identifier: MIT */
#ifndef BT3_WIRE_H
#define BT3_WIRE_H
#include <stddef.h>
/* Normalize shared protocol-5/6 packets (types 1..12) between native memory
 * and little-endian wire bytes. This is not the version-6 lobby/spectator codec.
 * big_host explicitly permits testing Xbox byte order on an x86 host.
 * Returns byte count, or 0 for unsupported/truncated messages. In-place is supported.
 * Does not implement simulation, rollback, authentication or matchmaking. */
size_t bt3_wire_encode(void *out,const void *native,size_t n,int big_host);
size_t bt3_wire_decode(void *native,const void *wire,size_t n,int big_host);
#endif
