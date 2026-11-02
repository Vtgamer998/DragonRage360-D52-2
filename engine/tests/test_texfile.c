/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "dr_texfile.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void put(unsigned char *p,uint64_t v,unsigned n) {for(unsigned i=0;i<n;i++)p[i]=(unsigned char)(v>>(i*8));}
int main(void)
{
    unsigned char file[1248]={0},rgba[1024],old[1024];
    put(file,1,4);put(file+4,8,4);put(file+32,24,4);put(file+40,1152,4);
    put(file+32+48,UINT64_C(4)<<26|UINT64_C(4)<<30,8);
    unsigned char *p=file+96;
    put(p+12,0x50000047,4);put(p+16,UINT64_C(0x1000000000008003),8);put(p+24,14,8);
    put(p+40,0x51,8);put(p+48,16|UINT64_C(16)<<32,8);put(p+56,0x52,8);put(p+72,0x53,8);
    put(p+80,UINT64_C(0x0800000000008040),8);
    for(unsigned i=0;i<1024;i++)p[96+i]=(unsigned char)i;
    put(p+1120,UINT64_C(0x1000000000008001),8);put(p+1128,14,8);put(p+1144,0x3f,8);
    assert(!dr_texfile_validate_transfers(file,sizeof(file),0));
    /* Even CT32 entries may carry a CLUT pointer: never publish an unchecked one. */
    put(file+36,0xffffffff,4);put(file+44,192,4);
    assert(dr_texfile_validate_transfers(file,sizeof(file),0));
    put(file+36,0,4);put(file+44,0,4);
    DrTexFileImage info;assert(!dr_texfile_decode(file,sizeof(file),0,rgba,sizeof(rgba),&info));
    assert(info.width==16 && info.height==16 && !info.psm && info.rgba_bytes==1024);
    for(unsigned i=0;i<1024;i++)assert(rgba[i]==(unsigned char)i);
    memset(rgba,0xa5,sizeof(rgba));memcpy(old,rgba,sizeof(old));
    for(size_t n=0;n<sizeof(file);n++) {
        assert(dr_texfile_decode(file,n,0,rgba,sizeof(rgba),&info));assert(!info.rgba_bytes);
        assert(!memcmp(rgba,old,sizeof(old)));
    }
    assert(dr_texfile_decode(file,sizeof(file),0,rgba,sizeof(rgba)-1,&info));
    put(p+32,1,8);assert(dr_texfile_decode(file,sizeof(file),0,rgba,sizeof(rgba),&info));put(p+32,0,8);
    put(file+32,0xffffffff,4);assert(dr_texfile_decode(file,sizeof(file),0,rgba,sizeof(rgba),&info));
    assert(!memcmp(rgba,old,sizeof(old)));
    unsigned char table[160]={0};put(table,2,4);put(table+4,8,4);
    uint64_t tex=UINT64_C(4)<<26|UINT64_C(4)<<30;uint32_t index;
    put(table+32+48,tex,8);put(table+96+48,tex,8);put(table+96+32,3,4);put(table+96+36,2,4);
    assert(!dr_texfile_material(table,sizeof(table),tex|(UINT64_C(1)<<34),&index) && index==0);
    assert(!dr_texfile_material(table,sizeof(table),tex|3|(UINT64_C(2)<<37),&index) && index==1);
    assert(dr_texfile_material(table,sizeof(table),tex|4,&index) && index==UINT32_MAX);
    put(table+96+32,0,4);put(table+96+36,0,4);
    assert(dr_texfile_material(table,sizeof(table),tex,&index) && index==UINT32_MAX);
    puts("PASS TexFile: CT32 pixels, every fixture truncation, capacity/offset/unsupported-transfer rejection, no partial RGBA output");
}
