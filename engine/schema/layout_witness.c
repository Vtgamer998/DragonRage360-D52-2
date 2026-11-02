/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Layout-only compilation; NEVER linked into the game.
 * Named classes documented in battle/btl_scene.h and battle/eft_b.h.
 * This forces otherwise-unused declarations into PPC DWARF without inventing
 * widths for anonymous tables or opaque blobs.
 */
#include "common.h"
#include "battle/btl_scene.h"
extern BtlTaskClass gBtlSceneRootClass;
extern BtlTaskClass gEftBubbleClass,gEftStageScrollClass,gEftGeyserMgrClass,gEftGeyserClass;
extern f32 gMathfSinCoef[4]; /* src/sys/mathf.c */
const void *const xb_layout_witness[]={&gBtlSceneRootClass,&gEftBubbleClass,
    &gEftStageScrollClass,&gEftGeyserMgrClass,&gEftGeyserClass,&gMathfSinCoef};
