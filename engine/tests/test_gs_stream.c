/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Real byte-stream consumer and reference VM; no GPU or battle mocks. */
#include "dr_gs_stream.h"
#include "dr_vu1.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static DrGsStream s;
static unsigned char received[256];
static unsigned received_bytes;
static int callback_error;
static void put(unsigned char *p,uint32_t x)
{for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(x>>(i*8));}
static int gif(void *arg,const unsigned char *p,uint32_t n)
{
    assert(arg==received && n && n*16<=sizeof(received)-received_bytes);
    if(callback_error)return callback_error;
    memcpy(received+received_bytes,p,n*16);received_bytes+=n*16;return 0;
}
static void reset(void)
{received_bytes=0;callback_error=0;assert(!dr_gs_stream_init(&s,gif,received));}
static int code(uint32_t word)
{unsigned char bytes[4];put(bytes,word);return dr_gs_stream_vif(&s,bytes,4);}
static void tag(unsigned char *p,unsigned id,unsigned qwc,uint32_t ref,uint32_t a,uint32_t b)
{put(p,id<<28|qwc);put(p+4,ref);put(p+8,a);put(p+12,b);}
int main(void)
{
    unsigned char store[257]={0},external[33]={0},next[17]={0};
    unsigned char *p=store+1,*e=external+1,*target=next+1;
    for(unsigned i=0;i<32;i++)e[i]=(unsigned char)(i*7+3);
    DrGsSpan spans[3]={{0x1000,p,256},{0x5000,e,32},{0x9000,target,16}};
    reset();tag(p,1,2,0,0x10000000,0x50000002);memcpy(p+16,e,32);tag(p+48,7,0,0,0,0);
    assert(!dr_gs_stream_chain(&s,spans,3,0x1000,1));
    assert(received_bytes==32 && !memcmp(received,e,32) && s.stats.tags==2 && s.stats.gif_quadwords==2);
    // Limits apply per chain; VU memory/programs persist between chains.
    assert(!dr_gs_stream_chain(&s,spans,3,0x1000,1) && received_bytes==64 && s.stats.tags==2);
    for(unsigned id=0;id<=4;id++) {
        if(id==1 || id==2)continue;
        reset();tag(p,id,2,0x5000,0,0x50000002);tag(p+16,7,0,0,0,0);
        assert(!dr_gs_stream_chain(&s,spans,3,0x1000,1));
        assert(received_bytes==32 && !memcmp(received,e,32));
    }
    reset();tag(p,2,2,0x9000,0,0x50000002);memcpy(p+16,e,32);tag(target,7,0,0,0,0);
    assert(!dr_gs_stream_chain(&s,spans,3,0x1000,1) && received_bytes==32);
    reset();tag(p,5,0,0x1040,0,0);tag(p+16,7,0,0,0,0);
    tag(p+64,6,1,0,0,0x50000001);memcpy(p+80,e,16);
    assert(!dr_gs_stream_chain(&s,spans,3,0x1000,1) && received_bytes==16);
    reset();tag(p,1,2,0,0x08000000,0x08000000);
    memset(p+16,0,12);put(p+28,0x50000001);memcpy(p+32,e,16);tag(p+48,7,0,0,0,0);
    assert(!dr_gs_stream_chain(&s,spans,3,0x1000,0) && received_bytes==16); // TTE disabled
    reset();assert(!code(0x50000002));
    assert(!dr_gs_stream_vif(&s,e,4) && !dr_gs_stream_vif(&s,e+4,12));
    assert(!dr_gs_stream_vif(&s,e+16,16) && !dr_gs_stream_finish(&s));
    assert(received_bytes==32 && !memcmp(received,e,32));

    // MPG + MSCAL and independent VIF controls preserve the double buffer.
    reset();unsigned char micro[16],vector[16],readback[16],zero[16]={0};
    put(micro,0x8000033c);put(micro+4,0x400002ff);put(micro+8,0x8000033c);put(micro+12,0x2ff);
    for(unsigned i=0;i<4;i++)put(vector+i*4,0x11223344+i);
    assert(!code(0x4a020000));assert(!dr_gs_stream_vif(&s,micro,4));assert(!dr_gs_stream_vif(&s,micro+4,12));
    assert(!code(0x03000022) && !code(0x020001ef) && !code(0x14000000));
    assert(s.stats.vu_calls==1 && !code(0x04000011)); // ITOP must not reset TOPS
    assert(!code(0x6c018000) && !dr_gs_stream_vif(&s,vector,16));
    assert(!dr_vu1_read(529*16,readback,16) && !memcmp(readback,vector,16));
    assert(!dr_vu1_read(34*16,readback,16) && !memcmp(readback,zero,16));
    assert(!code(0x14000000) && !code(0x03000063)); // BASE must not reset TOPS
    assert(!code(0x6c018000) && !dr_gs_stream_vif(&s,vector,16));
    assert(!dr_vu1_read(34*16,readback,16) && !memcmp(readback,vector,16));
    assert(!code(0x020001ef) && !code(0x6c018000) && !dr_gs_stream_vif(&s,vector,16));
    assert(!dr_vu1_read(99*16,readback,16) && !memcmp(readback,vector,16));
    assert(!dr_gs_stream_finish(&s));
    // Masked UNPACK selects ROW, retaining canonical byte order.
    reset();assert(!code(0x30000000) && !dr_gs_stream_vif(&s,vector,16));
    unsigned char mask[4];put(mask,0x55555555);
    assert(!code(0x20000000) && !dr_gs_stream_vif(&s,mask,4));
    assert(!code(0x7c010000) && !dr_gs_stream_vif(&s,zero,16));
    assert(!dr_vu1_read(0,readback,16) && !memcmp(readback,vector,16));
    // STCYCL filling: one input vector produces two output vectors.
    reset();assert(!code(0x01000201) && !code(0x6c020000));
    assert(!dr_gs_stream_vif(&s,vector,16) && !dr_gs_stream_finish(&s));
    assert(!dr_vu1_read(0,readback,16) && !memcmp(readback,vector,16));

    // NUM=0 denotes 256 V4_32 vectors, the largest pending payload.
    reset();unsigned char full[4096];for(unsigned i=0;i<1024;i++)put(full+i*4,i);
    assert(!code(0x6c000000) && !dr_gs_stream_vif(&s,full,2048));
    assert(!dr_gs_stream_vif(&s,full+2048,2048) && !dr_gs_stream_finish(&s));
    assert(!dr_vu1_read(255*16,readback,16) && !memcmp(readback,full+255*16,16));
    reset();s.vu_budget=8; // An uploaded program without E cannot run forever.
    put(micro+4,0x2ff);assert(!code(0x4a020000) && !dr_gs_stream_vif(&s,micro,16));
    assert(code(0x14000000)==DR_VU_BUDGET);
    reset();tag(p,2,0,0x1000,0,0);s.tag_budget=3;
    assert(dr_gs_stream_chain(&s,spans,3,0x1000,1)==DR_STREAM_BUDGET && !received_bytes);
    reset();tag(p,6,0,0,0,0);
    assert(dr_gs_stream_chain(&s,spans,3,0x1000,1)==DR_STREAM_STACK);
    reset();tag(p,5,0,0x1020,0,0);tag(p+32,5,0,0x1040,0,0);tag(p+64,5,0,0x1060,0,0);
    assert(dr_gs_stream_chain(&s,spans,3,0x1000,1)==DR_STREAM_STACK);
    reset();tag(p,0,3,0x5000,0,0x50000003);
    assert(dr_gs_stream_chain(&s,spans,3,0x1000,1)==DR_STREAM_RANGE && !received_bytes);
    reset();tag(p,0,1,0x5001,0,0x50000001);
    assert(dr_gs_stream_chain(&s,spans,3,0x1000,1)==DR_STREAM_RANGE);
    reset();DrGsSpan overlap[2]={{0x1000,p,32},{0x1010,e,16}};
    assert(dr_gs_stream_chain(&s,overlap,2,0x1000,1)==DR_STREAM_RANGE);
    reset();DrGsSpan wrap={0xfffffff0,p,32};
    assert(dr_gs_stream_chain(&s,&wrap,1,0xfffffff0,1)==DR_STREAM_RANGE);
    reset();DrGsSpan tail[2]={{0xfffffff0,p,16},{0x5000,e,32}};
    tag(p,0,1,0x5000,0,0x50000001);
    assert(!dr_gs_stream_chain(&s,tail,2,0xfffffff0,1) && received_bytes==16);
    reset();tag(p,3,0,0,0,0);
    assert(dr_gs_stream_chain(&s,tail,2,0xfffffff0,1)==DR_STREAM_RANGE);
    reset();assert(code(0x08000000)==DR_STREAM_COMMAND && code(0)==DR_STREAM_COMMAND);
    reset();assert(code(0x05000003)==DR_VU_INVALID);
    reset();assert(!code(0x4a020000) && !dr_gs_stream_vif(&s,micro,4));
    assert(dr_gs_stream_finish(&s)==DR_STREAM_TRUNCATED);
    reset();assert(!code(0x50000001) && !dr_gs_stream_vif(&s,e,4));
    assert(dr_gs_stream_finish(&s)==DR_STREAM_TRUNCATED);
    reset();s.word_budget=1;assert(!code(0) && code(0)==DR_STREAM_BUDGET);
    reset();callback_error=-499;assert(!code(0x50000001));
    assert(dr_gs_stream_vif(&s,e,16)==-499 && !received_bytes);
    reset();assert(dr_gs_stream_vif(&s,e,3)==DR_STREAM_INVALID);
    assert(code(0)==DR_STREAM_INVALID); // Invalid run is sticky too.
    reset();assert(dr_gs_stream_chain(&s,spans,3,0x1001,1)==DR_STREAM_RANGE);
    puts("PASS: bounded canonical-LE DMA/VIF, all eight chain IDs, TTE, split DIRECT/MPG, exact mappings, VU control/MSCAL/double buffering, masked/filling UNPACK, loop/word/stack/range/truncation/callback failures; no combat or GPU execution");
}
