/* SPDX-License-Identifier: AGPL-3.0-or-later
 * BobjBone format: upstream include/battle/bobj_a.h. Relative links remain relative.
 */
#include "dr_resource.h"
static unsigned half(const unsigned char *p) {return p[0]|(unsigned)p[1]<<8;}
static void swap_half(unsigned char *p) {unsigned char t=p[0];p[0]=p[1];p[1]=t;}
static void swap_word(unsigned char *p) {unsigned char t=p[0];p[0]=p[3];p[3]=t;t=p[1];p[1]=p[2];p[2]=t;}
int dr_skeleton_prepare(void *data,size_t size,int big_endian,size_t *nodes)
{
    unsigned char *p=data;size_t offsets[256],used=0,at=0;
    if(nodes) *nodes=0;
    if(!p || (big_endian!=0 && big_endian!=1)) return DR_RES_FORMAT;
    for(;;) {
        if(at>size || size-at<64) return DR_RES_TRUNCATED;
        if(used==256) return DR_RES_CAPACITY;
        offsets[used++]=at;
        if(half(p+at+6)) break;
        uint32_t next=dr_resource_le32(p+at);
        if(next<64 || (next&3)) return DR_RES_FORMAT;
        if(next>size-at) return DR_RES_TRUNCATED;
        at+=next;
    }
    if(big_endian) for(size_t i=0;i<used;i++) {
        unsigned char *v=p+offsets[i];swap_word(v);
        for(unsigned j=4;j<12;j+=2) swap_half(v+j);
        for(unsigned j=12;j<64;j+=4) swap_word(v+j);
    }
    if(nodes) *nodes=used;
    return DR_RES_OK;
}
