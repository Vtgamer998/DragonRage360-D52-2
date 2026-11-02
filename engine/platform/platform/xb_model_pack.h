/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef XB_MODEL_PACK_H
#define XB_MODEL_PACK_H
#include <stddef.h>
/* Typed USA model packs, relative offsets until original fixup. VIF/GIF
 * remain LE, with bounded texture rebasing done before publication. */
int XbModelPack_Prepare(void *,size_t,int big_endian);
int XbModelPack_RegisterInitial(void);
/* Original texture rebaser may skip only exact already-prepared chains from
 * a successful tracked read, with precisely the prepared destination bases. */
int XbModelPack_ChainPrepared(int,const void *,int,int,int,int,unsigned,unsigned);
#endif
