/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef XB_STAGE_PACK_H
#define XB_STAGE_PACK_H
#include <stddef.h>
typedef struct XbStagePackInfo {unsigned groups,materials,meshes,parts,nodes,cells,objects,timers,textures,entries;} XbStagePackInfo;
/* Stage0 only: atomic native relative file; original relocation remains required. */
int XbStagePack_Prepare(void*,size_t,XbStagePackInfo*);
int XbStagePack_RegisterInitial(void);
void *XbStagePack_NullMesh(void*);
void XbStagePack_RelocateTextureFrames(void*);
int XbStagePack_CheckRelocation(void*,size_t);
#endif
