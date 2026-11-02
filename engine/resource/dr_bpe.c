/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Bounded adaptation of the format decoded by upstream src/sys/bpe.c.
 * Original: dfdx047 / z3xox and contributors; see engine LICENSE.
 */
#include "dr_resource.h"
#include <string.h>
uint32_t dr_resource_le32(const void *p)
{
    const unsigned char *b=p;
    return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;
}
int dr_bpe_decode(const void *source,size_t src_size,void *destination,size_t capacity,size_t *written)
{
    const unsigned char *src=source;unsigned char *out=destination;
    size_t pos=8,produced=0,end,raw;
    if(written) *written=0;
    if(!src || src_size<8) return DR_RES_TRUNCATED;
    raw=dr_resource_le32(src);size_t packed=dr_resource_le32(src+4);
    if(raw>0x7fffffffu || packed>0x7fffffffu) return DR_RES_FORMAT;
    if(packed>src_size-8) return DR_RES_TRUNCATED;
    if(raw>capacity || (raw && !out)) return DR_RES_CAPACITY;
    end=packed+8;
    while(pos<end) {
        unsigned char left[256],right[256]={0},stack[512];
        for(unsigned i=0;i<256;i++) left[i]=(unsigned char)i;
        unsigned symbol=0;
        while(symbol<256) {
            if(pos==end) return DR_RES_TRUNCATED;
            unsigned count=src[pos++];
            if(count>127) {symbol+=count-127;count=0;}
            if(symbol>256) return DR_RES_FORMAT;
            if(symbol==256) break;
            if(count+1>256-symbol) return DR_RES_FORMAT;
            for(unsigned i=0;i<=count;i++,symbol++) {
                if(pos==end) return DR_RES_TRUNCATED;
                left[symbol]=src[pos++];
                if(left[symbol]!=symbol) {
                    if(pos==end) return DR_RES_TRUNCATED;
                    right[symbol]=src[pos++];
                }
            }
        }
        if(end-pos<2) return DR_RES_TRUNCATED;
        unsigned tokens=(unsigned)src[pos]<<8|src[pos+1];pos+=2;
        if(tokens>end-pos) return DR_RES_TRUNCATED;
        /* A valid binary expansion with L output leaves uses <=2*L nodes.
         * This budget and bounded stack also terminate cyclic dictionaries. */
        uint64_t budget=(uint64_t)(raw-produced)*2+tokens;
        for(unsigned t=0;t<tokens;t++) {
            unsigned depth=0;stack[depth++]=src[pos++];
            while(depth) {
                if(!budget--) return DR_RES_FORMAT;
                unsigned c=stack[--depth];
                if(left[c]==c) {
                    if(produced==raw) return DR_RES_FORMAT;
                    out[produced++]=(unsigned char)c;
                } else {
                    if(depth>sizeof(stack)-2) return DR_RES_FORMAT;
                    stack[depth++]=right[c];stack[depth++]=left[c];
                }
            }
        }
    }
    if(produced!=raw) return DR_RES_FORMAT;
    if(written) *written=produced;
    return DR_RES_OK;
}
