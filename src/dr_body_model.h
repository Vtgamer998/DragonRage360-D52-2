/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef DR_BODY_MODEL_H
#define DR_BODY_MODEL_H
#include <stddef.h>
#include <stdint.h>
enum {DR_MODEL_INVALID=-260,DR_MODEL_TRUNCATED=-261,DR_MODEL_CAPACITY=-262,DR_MODEL_VIF=-263};
typedef struct {
    size_t record_offset,record_bytes,chain_offset,chain_bytes;
    uint16_t parent,pop,last,node,enabled,flags; /* parent is output index, UINT16_MAX = root */
    float pivot_a[4],pivot_b[4],rest[4];
    uint32_t bound_words[8]; /* Raw numeric words; their physical semantics are not inferred here. */
} DrBodyPart;
typedef struct {uint32_t flags,parts,chains;size_t first_part;} DrBodyModel;
/* Read-only pmdl view: core skeleton, hierarchy, pivot data, body VIF spans.
 * All chains are validated before outputs are written. No pointer relocation,
 * animation binding, world-matrix calculation, texture rebasing or GPU drawing.
 * Views use native CPU numeric fields; VIF source bytes stay canonical LE. */
int dr_body_model_read(const void *,size_t,DrBodyPart *,size_t capacity,DrBodyModel *,size_t *bad_offset);
#endif
