/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef XB_STAGE_CAMERA_H
#define XB_STAGE_CAMERA_H
#include <stddef.h>
/* CMAn / DemoCamAnim, offsets remain relative until the original fixup.
 * Source and destination must be distinct. Failure leaves destination intact. */
int XbStageCamera_Prepare(void *destination,const void *source,size_t bytes);
int XbStageCamera_PrepareEndian(void *,const void *,size_t,int big_endian);
#endif
