/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef DR_RESOURCE_H
#define DR_RESOURCE_H
#include <stddef.h>
#include <stdint.h>
enum { DR_RES_OK=0,DR_RES_TRUNCATED=-1,DR_RES_FORMAT=-2,DR_RES_CAPACITY=-3 };
/* src_size must be the actual containing span, not an unchecked header value.
 * Buffers must not overlap. On failure dst can contain partial output. */
int dr_bpe_decode(const void *src,size_t src_size,void *dst,size_t capacity,size_t *written);
/* One freshly decoded little-endian BobjAnim, at most 0xC000 bytes.
 * Validates before modifying; call exactly once per newly decoded buffer.
 * big_endian=1 writes big-endian numeric fields, =0 leaves bytes unchanged.
 * Float/quaternion bit patterns preserved. No skeleton/model relocation. */
int dr_anim_prepare(void *data,size_t size,int big_endian);
/* BobjBone chain: validate all links before converting; at most 256 nodes.
 * No relocation, pose calculation, model header or skinning conversion. */
int dr_skeleton_prepare(void *,size_t,int big_endian,size_t *nodes);
uint32_t dr_resource_le32(const void *p);
int dr_resource_selftest(void);
#endif
