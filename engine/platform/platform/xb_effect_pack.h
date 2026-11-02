/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef XB_EFFECT_PACK_H
#define XB_EFFECT_PACK_H
#include "xb_character_effects.h"
/* Whole canonical file becomes native relative tables. Pointer relocation is
 * left to original consumers. The commit is atomic; no persistent sidecar. */
int XbEffectPack_Prepare(void *,size_t,XbCharacterEffectsInfo *);
int XbEffectPack_RegisterInitial(void);
#endif
