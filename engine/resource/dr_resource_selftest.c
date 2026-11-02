/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "dr_resource.h"
#include <string.h>
int dr_resource_selftest(void)
{
    unsigned char src[162]={0},decoded[148];size_t written=0;
    src[0]=148;src[4]=154;
    src[8]=254;src[9]=127;src[10]=254;src[11]=255;
    src[12]=0;src[13]=148;src[16]=30; /* raw animation length */
    if(dr_bpe_decode(src,sizeof(src),decoded,sizeof(decoded),&written) || written!=148) return 0;
    if(dr_anim_prepare(decoded,written,1) || decoded[2]!=0 || decoded[3]!=30) return 0;
    /* Validation must fail before altering a buffer with an out-of-range track. */
    decoded[6]=0xff;decoded[7]=0xff;
    unsigned char before[148];memcpy(before,decoded,sizeof(before));
    if(dr_anim_prepare(decoded,sizeof(decoded),1)==DR_RES_OK || memcmp(before,decoded,sizeof(before))) return 0;
    return 1;
}
