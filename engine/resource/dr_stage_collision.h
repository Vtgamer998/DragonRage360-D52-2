/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef DR_STAGE_COLLISION_H
#define DR_STAGE_COLLISION_H
#include <stddef.h>
#include <stdint.h>
/* Immutable canonical-LE stage block; no PS2 pointer is dereferenced. Offsets
 * are WORDS from entry 0, as gfxm_e_d.c / ColMesh_SetBase specify. This view
 * does not install gBtlStage or claim that the stage renderer is initialized. */
typedef struct DrStageCollision {
 const uint8_t *base;size_t size;
 uint32_t zones,zone_offset,objects,meshes,nodes,polygons;
 float radius,top,bottom;
} DrStageCollision;
typedef struct DrStageTriangle { uint32_t flags;float vertex[3][4],plane[4]; } DrStageTriangle;
enum { DR_STAGE_FORMAT=-80,DR_STAGE_BOUNDS=-81,DR_STAGE_TREE=-82,DR_STAGE_NUMBER=-83,DR_STAGE_MEMORY=-84 };
/* Fully validate static and destructible collision before publishing output.
 * On failure *out is zero; input remains unchanged. */
int dr_stage_collision_open(const void *pack,size_t bytes,DrStageCollision *out);
/* record=-1: static mesh; >=0: destructible record in that zone.
 * polygon index is the original index, NOT the tree leaf index. */
int dr_stage_collision_triangle(const DrStageCollision *,uint32_t zone,int record,uint32_t polygon,DrStageTriangle *out);
/* Closed AABB query, original left-before-right leaf order. callback receives
 * native float values and original polygon flags. No global traversal state. */
typedef int (*DrStageTriangleFn)(void *,uint32_t polygon,const DrStageTriangle *);
int dr_stage_collision_query(const DrStageCollision *,uint32_t zone,int record,
 const float min[3],const float max[3],DrStageTriangleFn,void *,int *result);
#endif
