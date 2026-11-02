/* SPDX-License-Identifier: MIT */
#ifndef XB_ADXT_H
#define XB_ADXT_H
#include <stddef.h>
#include <stdint.h>
void ADXT_Init(void);
int XbAdx_EnsureOutput(void);
void XbAdx_SetEffectsMixer(int (*)(float *),void (*)(void));
void *ADXT_Create(int,void *,int);
int XbAdx_Destroy(void *);
void ADXT_SetReloadSct(void *,int);
void ADXT_SetSvrFreq(void *,int);
void ADXT_StartAfs(void *,int,int);
void ADXT_StartFname(void *,char *);
void ADXT_Stop(void *);
void ADXT_Pause(void *,int);
void ADXT_SetOutVol(void *,int);
int ADXT_GetOutVol(void *);
void ADXT_SetOutPan(void *,int,int);
void ADXT_SetOutputMono(int);
int ADXT_GetStat(void *);
/* Media time of this player consumed by the SDK, quantized to 256/48000 s.
 * Does not count queued-ahead audio, silence from underruns or paused samples. */
int XbAdx_Clock(void *,uint64_t *,int *);
int XbAdx_LastError(void);
void XbAdx_Tick(void);
int XbAdx_Close(void);
/* Same-owner CPU mixer, 256 stereo samples in the first two planar channels
 * of an xsdk 6-channel float frame. Does not advance the game's tick clock. */
int XbAdx_Mix(float *);
/* Typed native source entry, also used to compare real ADX resources in tests. */
int XbAdx_StartBytes(void *,const void *,size_t);
#endif
