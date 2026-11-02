/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef XB_SOUND_BANK_H
#define XB_SOUND_BANK_H
#include <stddef.h>
#include <stdint.h>
typedef struct XbSoundBankInfo {
    uint32_t body,hd,sequence,end,max_sample;
    uint8_t loop[128];
} XbSoundBankInfo;
/* Header only is native; Sony HD/sequence/ADPCM payload stays LE. Inspect is
 * read-only and leaves info untouched on failure. mask=0 uses broad bounds
 * for metadata readers; converters require the actual original bank mask. */
int XbSoundBank_Inspect(const void *,size_t,unsigned mask,int header_big_endian,XbSoundBankInfo *);
/* Validate before mutation. No copied bank, side registry or new ownership. */
int XbSoundBank_Prepare(void *,size_t,unsigned mask,int target_big_endian);
int XbSoundBank_RegisterInitial(void);
int XbSoundBank_RegisterCommon(void);
int XbSoundBank_TryVagMax(const void *,int *);
int XbSoundBank_TryLoopFlags(const void *,void *);
/* Strict original loader: reject missing provenance before the sound consumer. */
int XbSoundBank_RequireReadBank(const void *,unsigned mask);
#endif
