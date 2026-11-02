/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef DR_FACE_VIF_H
#define DR_FACE_VIF_H
#include <stddef.h>
#include <stdint.h>
/* Face geometry entries referenced by BtlObj_BindTables -> mdl.mouthA[]:
 * inline DMA RET, followed by an END guard and zero alignment padding.
 * This parser handles the observed NOP / V4_32 UNPACK / MSCNT subset only.
 * VIF input assumes equal CL/WL; it does not execute MSCNT or skin a mesh.
 * All input and UNPACK payloads stay canonical little-endian. */
enum {DR_FACE_OK=0,DR_FACE_INVALID=-240,DR_FACE_TRUNCATED=-241,
      DR_FACE_UNSUPPORTED=-242,DR_FACE_CALLBACK=-243};
typedef struct {
    uint32_t commands,unpacks,vectors,continues,noops;
    size_t dma_payload_bytes,guard_offset;
} DrFaceVifStats;
typedef struct {
    uint32_t code,command,num,immediate;
    size_t code_offset,data_offset,data_bytes;
} DrFaceVifCommand;
typedef int (*DrFaceVifVisitor)(void *,const DrFaceVifCommand *);
/* Validates the WHOLE block before invoking a visitor. Stats are zero on format
 * error. Visitor side effects cannot be undone if the visitor itself fails.
 * Caller must keep source bytes unchanged for both passes. bad_offset is
 * SIZE_MAX on success or visitor failure. No pointers are relocated in input. */
int dr_face_vif_parse(const void *,size_t,DrFaceVifVisitor,void *,
                      DrFaceVifStats *,size_t *bad_offset);
#endif
