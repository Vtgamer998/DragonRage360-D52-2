/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef XB_STAGE_EFFECTS_H
#define XB_STAGE_EFFECTS_H
#include <stddef.h>
/* Zero-based pack indices 6 (scroll mesh),12 (surfaces),21 (wall colour),22 (wall params).
 * Destination is private staging memory; discard it on any error. */
int XbStageEffects_PrepareEntry(unsigned,void *,const void *,size_t);
#endif
