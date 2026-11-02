/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef DR_OT_NATIVE_H
#define DR_OT_NATIVE_H
#include <stddef.h>
/* Native OT packet -> native-u64 DIRECT representation. The DMA producer then
 * serializes these numeric u64 words to LE. Does not modify borrowed packets.
 * dst=NULL preflights. Only reviewed PACKED A+D/NOP and typed REGLIST registers. */
int dr_ot_payload(void *,const void *,size_t,int);
#endif
