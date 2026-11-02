/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Format: upstream include/battle/bobj_a.h, BobjAnim/Track/Event/Shape.
 * Never byte-swap a full buffer: mixed u8/u16/u32 fields need distinct treatment.
 */
#include "dr_resource.h"
enum { HEADER=0x94,NODES=71,MAX_RANGES=1+71+255 };
typedef struct {size_t off,len;unsigned kind,count;} Range;
static unsigned le16(const unsigned char *p) { return p[0]|(unsigned)p[1]<<8; }
static void swap16(unsigned char *p) {unsigned char t=p[0];p[0]=p[1];p[1]=t;}
static void swap32(unsigned char *p) {swap16(p);swap16(p+2);unsigned char a=p[0],b=p[1];p[0]=p[2];p[1]=p[3];p[2]=a;p[3]=b;}
static int add(Range *r,unsigned *used,size_t off,size_t len,unsigned kind,unsigned count,size_t size)
{
    if(off>size || len>size-off) return DR_RES_TRUNCATED;
    for(unsigned i=0;i<*used;i++) {
        if(off==r[i].off && len==r[i].len && kind==r[i].kind) return DR_RES_OK;
        if(off<r[i].off+r[i].len && r[i].off<off+len) return DR_RES_FORMAT;
    }
    if(*used==MAX_RANGES) return DR_RES_FORMAT;
    r[(*used)++]=(Range){off,len,kind,count};return DR_RES_OK;
}
int dr_anim_prepare(void *data,size_t size,int big_endian)
{
    unsigned char *p=data;Range ranges[MAX_RANGES];unsigned used=0;
    if(!p || size<HEADER) return DR_RES_TRUNCATED;
    if(size>0xC000 || (big_endian!=0 && big_endian!=1)) return DR_RES_FORMAT;
    unsigned events=p[1];size_t header_end=HEADER+(size_t)events*16;
    int error=add(ranges,&used,0,header_end,0,events,size);if(error) return error;
    for(unsigned i=0;i<NODES;i++) {
        size_t off=(size_t)le16(p+6+i*2)*4;
        if(!off) continue;
        if(off>size || size-off<4) return DR_RES_TRUNCATED;
        unsigned flags=le16(p+off),count=le16(p+off+2);
        if(!count) return DR_RES_FORMAT;
        unsigned kind=(flags&1)?1:2;
        size_t len=4+(size_t)count*(kind==1?10:24);
        error=add(ranges,&used,off,len,kind,count,size);if(error) return error;
    }
    for(unsigned i=0;i<events;i++) {
        const unsigned char *e=p+HEADER+i*16;
        if(dr_resource_le32(e)&1) {
            size_t off=(size_t)le16(e+10)*4;
            error=add(ranges,&used,off,28,3,7,size);if(error) return error;
        }
    }
    if(!big_endian) return DR_RES_OK;
    /* Validation complete. Shared tracks/shapes appear once in ranges. */
    for(unsigned i=0;i<used;i++) {
        Range *r=&ranges[i];unsigned char *v=p+r->off;
        if(r->kind==0) {
            for(unsigned j=2;j<HEADER;j+=2) swap16(v+j);
            for(unsigned j=0;j<r->count;j++) {
                unsigned char *e=v+HEADER+j*16;
                swap32(e);swap32(e+4);swap16(e+8);swap16(e+10);swap32(e+12);
            }
        } else if(r->kind==1) {
            swap16(v);swap16(v+2);
            for(unsigned j=0;j<r->count*2;j++) swap32(v+4+j*4);
            for(unsigned j=0;j<r->count;j++) swap16(v+4+r->count*8+j*2);
        } else if(r->kind==2) {
            swap16(v);swap16(v+2);
            for(unsigned j=0;j<r->count*6;j++) swap32(v+4+j*4);
        } else {
            for(unsigned j=0;j<7;j++) swap32(v+j*4);
        }
    }
    return DR_RES_OK;
}
