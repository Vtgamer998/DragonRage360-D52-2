/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "dr_gs_native.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
extern int sceGsSetDefDBuff(void *,int,int,int,int,int,int);
extern int sceGsSwapDBuff(void *,int);
extern int sceGsPutDrawEnv(uint64_t *);
extern int gGsMainFbp;
static unsigned char received[1024];static size_t used;
static uint64_t le64(const unsigned char *p){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(i*8);return v;}
static int gif(void *opaque,const unsigned char *p,uint32_t n){(void)opaque;assert(n*16<=sizeof(received)-used);memcpy(received+used,p,n*16);used+=n*16;return 0;}
int main(void) {
    uint64_t db[0x230/8],copy[0x230/8];
    assert(!dr_gs_native_open(gif,NULL));
    assert(!sceGsSetDefDBuff(db,0,512,448,3,0x31,1));
    db[(0xe0+0x20)/8]=0x3f80000011223344ull; /* game-edited clear color */
    memcpy(copy,db,sizeof(db));assert(!sceGsSwapDBuff(db,0));
    assert(used==240 && gGsMainFbp==0 && !memcmp(copy,db,sizeof(db)));
    assert(le64(received)==0x100000000000800eull && le64(received+8)==14);
    assert(le64(received+16)==8ull<<16 && le64(received+24)==0x4c);
    assert(le64(received+32)==(224ull|1ull<<24) && le64(received+40)==0x4e);
    assert(le64(received+48)==0x720000007000ull && le64(received+56)==0x18);
    assert(le64(received+176)==0x3f80000011223344ull && le64(received+184)==1);
    assert(!sceGsSwapDBuff(db,1) && used==480 && gGsMainFbp==112);
    assert((le64(received+256)&511)==112 && !memcmp(copy,db,sizeof(db)));
    assert(!sceGsSetDefDBuff(db,2,512,448,2,0x32,0));
    assert(!sceGsSwapDBuff(db,0) && used==624); /* eight register writes, no clear */
    assert(!dr_gs_native_close());
    assert(!dr_gs_native_open(gif,NULL));used=0;
    memcpy(db,copy,sizeof(db));db[0x50/8]=(db[0x50/8]&~0x7fffull)|15;
    assert(sceGsPutDrawEnv(db+0x50/8)==DR_STREAM_INVALID && !used);assert(!dr_gs_native_close());
    assert(!dr_gs_native_open(gif,NULL));memcpy(db,copy,sizeof(db));
    assert(sceGsSetDefDBuff(db,0,0,448,3,0x31,1)==DR_STREAM_INVALID);
    assert(!memcmp(db,copy,sizeof(db)) && !used);assert(!dr_gs_native_close());
    puts("PASS: native DBuff clear/no-clear, both frame buffers, game-edited u64 values, exact canonical GIF, preserved source and invalid-packet bounds; host transport only");
}
