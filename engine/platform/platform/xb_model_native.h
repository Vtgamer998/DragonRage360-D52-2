/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef XB_MODEL_NATIVE_H
#define XB_MODEL_NATIVE_H
#include <stddef.h>
typedef struct XbNativeModel XbNativeModel;
/* Native CPU model lifecycle resources; source VIF/GIF payloads stay LE.
 * Does not prepare roster/common/AI tables or authorize battle simulation. */
int XbNativeModel_Prepare(const void *,size_t,XbNativeModel **);
void XbNativeModel_Free(XbNativeModel *);
void XbNativeModel_BindTables(XbNativeModel *,void *object,const void *mdl,size_t);
void XbNativeModel_BindFiles(XbNativeModel *,void *object);
unsigned XbNativeModel_Parts(XbNativeModel *);
int XbNativeModel_Mesh(XbNativeModel *,unsigned,const void **,size_t *);
int XbNativeModel_ChainSpan(XbNativeModel *,const void *,size_t *);
/* Native TexFile for original consumers; pix/clut pointers are rebuilt. */
int XbNativeTexture_Prepare(const void *,size_t,void **);
void XbNativeTexture_Free(void *);
void *XbNativeModel_Header(XbNativeModel *);
#endif
