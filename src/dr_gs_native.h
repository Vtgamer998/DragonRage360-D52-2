/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef DR_GS_NATIVE_H
#define DR_GS_NATIVE_H
#include "dr_gs_stream.h"
/* Single-thread, renderer-owned session. The sole VU interpreter is shared by
 * native producer submission and legacy Port_Gs* entry points. Bind copies
 * only descriptors: canonical LE buffers must stay alive through the call.
 * Raw engine pointers and mixed-endian packets are never inferred/mapped. */
int dr_gs_native_open(DrStreamGif,void *);
int dr_gs_native_limits(uint32_t tags,uint32_t words,uint32_t instructions);
int dr_gs_native_close(void);
int dr_gs_native_reset(void); /* explicit path reset, refuses active/bound ownership */
int dr_gs_native_bind(const DrGsSpan *,size_t);
void dr_gs_native_unbind(void);
int dr_gs_native_vif1(uint32_t,int);
int dr_gs_native_gif(uint32_t,uint32_t,int);
int dr_gs_native_error(void);
int dr_gs_native_reject(int);
int dr_gs_native_stats(DrGsStreamStats *);
/* Original engine entry points. Errors are available through the session. */
void Port_GsVif1Chain(uint32_t,int);
void Port_GsGifChannel(uint32_t,uint32_t,int);
#endif
