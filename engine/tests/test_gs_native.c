/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "dr_gs_native.h"
#include "dr_vu1.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char received[512];static unsigned used;static int callback_error,reenter;
static int gif(void *opaque,const unsigned char *p,uint32_t n) {
    assert(opaque==received && n && n*16<=sizeof(received)-used);
    if(callback_error)return callback_error;
    if(reenter){assert(dr_gs_native_close()==DR_STREAM_INVALID);dr_gs_native_unbind();return 0;}
    memcpy(received+used,p,n*16);used+=n*16;return 0;
}
static void put(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static void tag(unsigned char *p,unsigned id,unsigned qwc,unsigned ref){memset(p,0,16);put(p,id<<28|qwc);put(p+4,ref);}
static void start(void){used=0;callback_error=reenter=0;assert(!dr_gs_native_open(gif,received));}
int main(void) {
    unsigned char bytes[256]={0},external[32],readback[16];
    start();assert(!dr_gs_native_limits(8192,2359296,250000));
    assert(dr_gs_native_limits(0,2359296,250000)==DR_STREAM_INVALID);
    assert(dr_gs_native_limits(65537,1,1)==DR_STREAM_INVALID);
    assert(dr_gs_native_limits(1,4194305,1)==DR_STREAM_INVALID);
    assert(dr_gs_native_limits(1,1,4000001)==DR_STREAM_INVALID);
    assert(!dr_gs_native_close());

    for(unsigned i=0;i<32;i++)external[i]=(unsigned char)(i*11+7);
    start();assert(!dr_gs_native_limits(8192,4,250000));assert(!dr_gs_native_reset());
    DrGsSpan small={0x5000,external,32};assert(!dr_gs_native_bind(&small,1));
    assert(dr_gs_native_gif(0x5000,2,0)==DR_STREAM_BUDGET);assert(!dr_gs_native_close());

    DrGsSpan spans[2]={{0x1000,bytes,sizeof(bytes)},{0x5000,external,sizeof(external)}};
    assert(dr_gs_native_error()==DR_STREAM_INVALID);assert(dr_gs_native_open(NULL,NULL)==DR_STREAM_INVALID);
    start();assert(dr_gs_native_open(gif,received)==DR_STREAM_INVALID);
    tag(bytes,7,3,0);put(bytes+16,0x4a020000);
    put(bytes+20,0x8000033c);put(bytes+24,0x400002ff);put(bytes+28,0x8000033c);put(bytes+32,0x2ff);
    put(bytes+36,0x6c010001);memcpy(bytes+40,external,16);put(bytes+56,0x14000000);
    assert(!dr_gs_native_bind(spans,2) && !dr_gs_native_vif1(0x1000,0));
    assert(!dr_vu1_read(16,readback,16) && !memcmp(readback,external,16));
    DrGsStreamStats stats;assert(!dr_gs_native_stats(&stats) && stats.vu_calls==1);dr_gs_native_unbind();
    assert(!dr_gs_native_bind(spans,2) && !dr_gs_native_gif(0x5000,2,0));
    assert(used==32 && !memcmp(received,external,32));dr_gs_native_unbind();
    assert(!dr_vu1_read(16,readback,16) && !memcmp(readback,external,16));
    memset(bytes,0,sizeof(bytes));tag(bytes,7,1,0);put(bytes+16,0x14000000);
    assert(!dr_gs_native_bind(spans,2) && !dr_gs_native_vif1(0x1000,0));
    assert(!dr_gs_native_stats(&stats) && stats.vu_calls==1);dr_gs_native_unbind();
    DrGsSpan desc=spans[1];assert(!dr_gs_native_bind(&desc,1));desc.bytes=NULL;desc.size=0;
    assert(!dr_gs_native_gif(0x5000,1,0));dr_gs_native_unbind();
    assert(dr_gs_native_gif(0x5000,1,0)==DR_STREAM_RANGE);
    assert(dr_gs_native_bind(spans,2)==DR_STREAM_RANGE);assert(!dr_gs_native_close());
    for(unsigned id=0;id<8;id++) {
        start();memset(bytes,0,sizeof(bytes));
        if(id==0 || id==3 || id==4){tag(bytes,id,2,0x5000);tag(bytes+16,7,0,0);}
        else if(id==6){tag(bytes,5,0,0x1040);tag(bytes+16,7,0,0);tag(bytes+64,6,2,0);memcpy(bytes+80,external,32);}
        else {tag(bytes,id,2,0x1040);memcpy(bytes+16,external,32);tag(bytes+48,7,0,0);tag(bytes+64,id==5?6:7,0,0);}
        put(bytes+8,0x08000000);put(bytes+12,0x08000000); /* TTE is ignored on GIF */
        assert(!dr_gs_native_bind(spans,2) && !dr_gs_native_gif(0x1000,0,1));
        assert(used==32 && !memcmp(received,external,32));assert(!dr_gs_native_close());
    }
    start();DrGsSpan overlap[2]={{0x1000,bytes,32},{0x1010,external,32}};
    assert(dr_gs_native_bind(overlap,2)==DR_STREAM_RANGE);assert(!dr_gs_native_close());
    start();DrGsSpan overflow={0xfffffff0,bytes,32};
    assert(dr_gs_native_bind(&overflow,1)==DR_STREAM_RANGE);assert(!dr_gs_native_close());
    start();assert(!dr_gs_native_bind(spans,2));assert(dr_gs_native_gif(0x5001,1,0)==DR_STREAM_INVALID);assert(!dr_gs_native_close());
    start();assert(!dr_gs_native_bind(spans,2));assert(dr_gs_native_gif(0x5000,3,0)==DR_STREAM_RANGE);assert(!dr_gs_native_close());
    start();assert(!dr_gs_native_bind(spans,2));callback_error=-499;
    assert(dr_gs_native_gif(0x5000,1,0)==-499 && !used);assert(!dr_gs_native_close());
    start();assert(!dr_gs_native_bind(spans,2));reenter=1;
    assert(dr_gs_native_gif(0x5000,1,0)==DR_STREAM_INVALID);assert(!dr_gs_native_close());
    start();tag(bytes,2,0,0x1000);assert(!dr_gs_native_bind(spans,2));
    assert(dr_gs_native_gif(0x1000,0,1)==DR_STREAM_BUDGET);assert(!dr_gs_native_close());
    start();tag(bytes,6,0,0);assert(!dr_gs_native_bind(spans,2));
    assert(dr_gs_native_gif(0x1000,0,1)==DR_STREAM_STACK);assert(!dr_gs_native_close());
    puts("PASS: native session, VIF uploads/MSCAL and shared VU persistence, direct GIF and eight DMA IDs, descriptor lifetimes, bounds/cycles/stack/callback errors and re-entry guards; host only");
}
