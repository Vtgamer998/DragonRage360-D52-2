/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef XB_CHARACTER_EFFECTS_H
#define XB_CHARACTER_EFFECTS_H
#include <stddef.h>
typedef struct XbCharacterEffects XbCharacterEffects;
typedef struct XbCharacterEffectsInfo {unsigned manager_entries,emitter_sets,textures,sprite_packs,cameras,prepared_technique_packs,unprepared_technique_packs;} XbCharacterEffectsInfo;
/* File 2 entry 6 and technique packs with the bounded scalar entry-3 variant. */
int XbCharacterEffects_Prepare(const void *,size_t,XbCharacterEffects **,XbCharacterEffectsInfo *);
void *XbCharacterEffects_Root(XbCharacterEffects *);
void *XbCharacterEffects_TechniqueRoot(XbCharacterEffects *,unsigned);
void XbCharacterEffects_Free(XbCharacterEffects *);
#endif
