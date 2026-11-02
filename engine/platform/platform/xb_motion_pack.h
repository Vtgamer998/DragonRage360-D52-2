/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef XB_MOTION_PACK_H
#define XB_MOTION_PACK_H
#include <stddef.h>
/* USA character motion tables (414 required slots, up to 512 entries).
 * Validates every compressed
 * motion before changing only the outer offset table. Packets remain LE.
 * Refusal leaves source unchanged; no retained allocation or extra owner. */
int XbMotionPack_Prepare(void *,size_t,int big_endian);
int XbMotionPack_RegisterInitial(void);
#endif
