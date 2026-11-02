/* SPDX-License-Identifier: MIT
 * Protocol field positions verified in Tenkaichi3DecompAndroid net.c,
 * commit 3ca419c7966b11438e2dc3c9a7e1914f102ed545; fingerprint packet verified
 * against official z3xox/Tenkaichi3Decomp 7b43a75bd8b0aeabcce0a22c45e1ad66162ac9d7.
 * Numeric fields are LE on wire;
 * names, chat UTF-8 and 18-byte pad records remain byte arrays.
 */
#include "bt3_wire.h"
#include <stdint.h>
#include <string.h>
static uint64_t number(const uint8_t *p,unsigned bytes,int big)
{
    uint64_t v=0;
    for(unsigned i=0;i<bytes;i++) v|=(uint64_t)p[big?bytes-1-i:i]<<(i*8);
    return v;
}
static void write_number(uint8_t *p,unsigned bytes,uint64_t v,int big)
{
    for(unsigned i=0;i<bytes;i++) p[big?bytes-1-i:i]=(uint8_t)(v>>(i*8));
}
static void field(uint8_t *p,size_t offset,unsigned bytes,int source_big,int dest_big)
{
    uint64_t v=number(p+offset,bytes,source_big);
    write_number(p+offset,bytes,v,dest_big);
}
static size_t convert(void *dst,const void *src,size_t n,int source_big,int dest_big)
{
    if(!dst || !src || n<8 || n>2048) return 0;
    const uint8_t *s=src;
    if(number(s,4,source_big)!=0x4E335442u) return 0;
    uint32_t type=(uint32_t)number(s+4,4,source_big);
    size_t header=8;
    switch(type) {
    case 1: if(n<12) return 0; header=12;break;
    case 2: if(n<12 || (n>12 && n<20)) return 0;header=n==12?12:(n<24?20:24);break;
    case 3: {
        if(n<44) return 0;
        uint32_t count=(uint32_t)number(s+12,4,source_big);
        if(count>16 || n!=44+(size_t)count*18) return 0;
        header=36;break;
    }
    case 4: if(n!=8 && n!=12) return 0;header=n;break;
    case 5: case 6: if(n!=12) return 0;header=12;break;
    case 7: if(n<16) return 0;header=n<24?(n/4)*4:24;break;
    case 8: case 9: if(n!=8) return 0;break;
    case 10: if(n<16 || number(s+12,4,source_big)!=n-16) return 0;header=16;break;
    case 11: if(n!=16) return 0;header=16;break;
    /* The digest travels as TWO u32 words, low first, not a native u64.
     * Swapping it as a u64 on Xbox would exchange its two halves. */
    case 12: if(n!=20 || number(s+8,4,source_big)!=0x46505231u)return 0;header=20;break;
    default: return 0;
    }
    uint8_t *d=dst;memmove(d,s,n);
    for(size_t i=0;i<header;i+=4) field(d,i,4,source_big,dest_big);
    if(type==3) field(d,36,8,source_big,dest_big);
    if(type==1) {
        if(n>=36) field(d,32,4,source_big,dest_big);
        if(n>=40) field(d,36,4,source_big,dest_big);
    }
    if(type==2 || type==7) {
        if(n>=48) field(d,44,4,source_big,dest_big);
        if(n>=52) field(d,48,4,source_big,dest_big);
    }
    return n;
}
size_t bt3_wire_encode(void *out,const void *native,size_t n,int big_host)
{return convert(out,native,n,!!big_host,0);}
size_t bt3_wire_decode(void *native,const void *wire,size_t n,int big_host)
{return convert(native,wire,n,0,!!big_host);}
