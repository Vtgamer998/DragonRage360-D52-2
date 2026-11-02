/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Character pack format: upstream port/tools/make_fingerprint.py pack_strict_table.
 */
#include "dr_container.h"
#include <string.h>
int dr_pack_entry(const dr_disc *disc,const dr_span *pack,uint32_t index,dr_span *out,unsigned *flags)
{
    unsigned char data[512];uint32_t count,previous=0,start=0,end=0,tag=0;int error;
    if(!out) return DR_FORMAT;
    memset(out,0,sizeof(*out));if(flags) *flags=0;
    if(!disc || !pack || pack->directory) return DR_FORMAT;
    error=dr_read(disc,pack,0,data,4);if(error) return error;
    count=dr_resource_le32(data);
    if(!count || count>4096 || (uint64_t)(count+2)*4>pack->size) return DR_FORMAT;
    if(index>=count) return DR_NOT_FOUND;
    uint32_t table_end=(count+2)*4;
    for(uint32_t first=0;first<=count;) {
        uint32_t words=count+1-first;if(words>sizeof(data)/4) words=sizeof(data)/4;
        error=dr_read(disc,pack,4+(uint64_t)first*4,data,(size_t)words*4);if(error) return error;
        for(uint32_t j=0;j<words;j++) {
            uint32_t at=first+j,word=dr_resource_le32(data+j*4),offset=word&~3u;
            if(offset<table_end || offset>pack->size || (at && offset<previous)) return DR_FORMAT;
            previous=offset;
            if(at==index) {start=offset;tag=word&3;}
            if(at==index+1) end=offset;
        }
        first+=words;
    }
    if(start==end) return DR_NOT_FOUND;
    out->offset=pack->offset+start;out->size=end-start;
    if(flags) *flags=tag;
    return DR_OK;
}
typedef struct {const unsigned char *bytes;size_t size;} Memory;
static int memory_read(void *opaque,uint64_t offset,void *out,size_t n)
{
    const Memory *m=opaque;
    if(offset>m->size || n>m->size-(size_t)offset) return -1;
    memcpy(out,m->bytes+(size_t)offset,n);return 0;
}
int dr_pack_memory_entry(const void *data,size_t size,uint32_t index,size_t *offset,size_t *length,unsigned *flags)
{
    if(offset) *offset=0;
    if(length) *length=0;
    if(flags) *flags=0;
    if(!data || !offset || !length) return DR_FORMAT;
    Memory memory={data,size};dr_disc disc={.read=memory_read,.opaque=&memory,.size=size};
    dr_span pack={.size=size},entry;
    int error=dr_pack_entry(&disc,&pack,index,&entry,flags);
    if(!error) {*offset=(size_t)entry.offset;*length=(size_t)entry.size;}
    return error;
}
int dr_pack_memory_span(const void *data,size_t size,const void *entry,size_t *length)
{
    if(length) *length=0;
    if(!data || !entry || !length) return DR_FORMAT;
    uintptr_t base=(uintptr_t)data,address=(uintptr_t)entry;
    if(address<base || address-base>=size) return DR_NOT_FOUND;
    size_t off=0,len=0;int error=dr_pack_memory_entry(data,size,0,&off,&len,NULL);
    if(error && error!=DR_NOT_FOUND) return error;
    const unsigned char *p=data;uint32_t count=dr_resource_le32(p);
    for(uint32_t i=0;i<count;i++) {
        uint32_t start=dr_resource_le32(p+4+i*4)&~3u;
        uint32_t end=dr_resource_le32(p+8+i*4)&~3u;
        if(start==address-base && end>start) {*length=end-start;return DR_OK;}
    }
    return DR_NOT_FOUND;
}
int dr_motion_from_memory(const void *source,size_t size,int compressed,int big_endian,void *out,size_t capacity,size_t *written)
{
    size_t bytes=0;
    if(written) *written=0;
    if(!source || !out || (compressed!=0 && compressed!=1) || (big_endian!=0 && big_endian!=1)) return DR_FORMAT;
    if(compressed) {
        if(size<8) return DR_RANGE;
        if(dr_resource_le32(source)>0xc000) return DR_RANGE;
        if(dr_bpe_decode(source,size,out,capacity,&bytes)) return DR_LOAD_DECODE;
    } else {
        if(size>capacity || size>0xc000) return DR_RANGE;
        memcpy(out,source,size);bytes=size;
    }
    if(!bytes) return DR_LOAD_EMPTY;
    if(dr_anim_prepare(out,bytes,big_endian)) return DR_LOAD_ANIMATION;
    if(written) *written=bytes;
    return DR_OK;
}
int dr_load_motion(const dr_disc *disc,const dr_span *entry,int compressed,int big_endian,
                   void *scratch,size_t scratch_cap,void *out,size_t out_cap,size_t *written)
{
    unsigned char header[8];size_t bytes=0;int error;
    if(written) *written=0;
    if(!disc || !entry || entry->directory || !out || (compressed!=0 && compressed!=1) ||
       (big_endian!=0 && big_endian!=1)) return DR_FORMAT;
    if(compressed) {
        error=dr_read(disc,entry,0,header,sizeof(header));if(error) return error;
        uint32_t raw=dr_resource_le32(header),packed=dr_resource_le32(header+4);
        if(raw>0xc000 || raw>out_cap || packed>0x7fffffffu) return DR_RANGE;
        uint64_t total=8+(uint64_t)packed;
        if(total>entry->size || total>scratch_cap || !scratch) return DR_RANGE;
        error=dr_read(disc,entry,0,scratch,(size_t)total);if(error) return error;
        /* Pass the actual verified subspan, never bytes beyond the pack entry. */
        if(dr_bpe_decode(scratch,(size_t)total,out,out_cap,&bytes)) return DR_LOAD_DECODE;
    } else {
        if(entry->size>out_cap || entry->size>0xc000) return DR_RANGE;
        bytes=(size_t)entry->size;
        error=dr_read(disc,entry,0,out,bytes);if(error) return error;
    }
    if(!bytes) return DR_LOAD_EMPTY;
    if(dr_anim_prepare(out,bytes,big_endian)) return DR_LOAD_ANIMATION;
    if(written) *written=bytes;
    return DR_OK;
}
int dr_container_selftest(void)
{
    /* One BPE animation in a strict character pack. No game assets. */
    unsigned char pack[180]={0},scratch[164],out[148];size_t off,len,written;
    pack[0]=1;pack[4]=12;pack[8]=180;
    unsigned char *p=pack+12;p[0]=148;p[4]=154;
    p[8]=254;p[9]=127;p[10]=254;p[11]=255;p[13]=148;p[16]=30;
    if(dr_pack_memory_entry(pack,sizeof(pack),0,&off,&len,NULL) || off!=12 || len!=168) return 0;
    Memory memory={pack,sizeof(pack)};dr_disc disc={.read=memory_read,.opaque=&memory,.size=sizeof(pack)};
    dr_span entry={.offset=off,.size=len};
    if(dr_load_motion(&disc,&entry,1,1,scratch,sizeof(scratch),out,sizeof(out),&written) || written!=148 || out[3]!=30) return 0;
    /* Same payload cannot borrow bytes from the next entry / surrounding file. */
    entry.size=10;
    if(dr_load_motion(&disc,&entry,1,1,scratch,sizeof(scratch),out,sizeof(out),&written)!=DR_RANGE || written!=0) return 0;
    unsigned char bone[64]={0};size_t nodes=0;bone[6]=1;
    return dr_skeleton_prepare(bone,sizeof(bone),1,&nodes)==DR_RES_OK && nodes==1 && bone[6]==0 && bone[7]==1;
}
