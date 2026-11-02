/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef XB_SFX_H
#define XB_SFX_H
#include <stdint.h>
#include <stddef.h>
int XbSfx_Bind(void *,int,int);
int XbSfx_Mix(float *planar_6_channels);
int XbSfx_LastError(void);
int XbSfx_ActiveVoices(void);
void XbSfx_Close(void);
int func_002967C0(int,int,...);
int sceSifCallRpc(void *,int,int,void *,int,void *,int,void *,void *);
/* Keep owned banks alive until Snd_UnloadBank and deferred uploads end. */
typedef struct XbSoundBank XbSoundBank;
int XbSfx_PrepareBank(const void *,size_t,int,XbSoundBank **);
void *XbSfx_BankData(XbSoundBank *);
void XbSfx_FreeBank(XbSoundBank *);
int XbSfx_TryVagMax(const void *,int *);
int XbSfx_TryLoopFlags(const void *,void *);
#endif
