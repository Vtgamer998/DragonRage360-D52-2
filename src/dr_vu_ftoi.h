/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef DR_VU_FTOI_H
#define DR_VU_FTOI_H
#include <stdint.h>
/* VU FTOI0/4/12/15 truncate and saturate by sign, including exponent 255.
 * Integer decode avoids NaN casts, floating exception behavior and double math.
 * Semantics cross-checked with PCSX2 VUops.cpp floatToInt; implementation independent. */
static inline uint32_t dr_vu_ftoi(uint32_t bits,unsigned fraction){
    int exponent=(int)((bits>>23)&255)-127+(int)fraction;
    if(exponent>=31)return (bits&0x80000000u)?0x80000000u:0x7fffffffu;
    if(exponent<0)return 0;
    uint32_t magnitude=(bits&0x7fffffu)|0x800000u;
    magnitude=exponent>=23?magnitude<<(exponent-23):magnitude>>(23-exponent);
    return (bits&0x80000000u)?0u-magnitude:magnitude;
}
#endif
