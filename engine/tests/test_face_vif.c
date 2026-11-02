/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "dr_face_vif.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned calls;
static int stop;
static int visit(void *ctx,const DrFaceVifCommand *c)
{
    assert(ctx==&calls);calls++;
    if(c->command==0x6c)assert(c->data_offset==12 && c->data_bytes==16 && c->immediate==5);
    return stop;
}
static void word(unsigned char *p,unsigned v)
{for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
int main(void)
{
    unsigned char data[64]={0},copy[64];
    word(data,0x60000002);word(data+8,0x6c010005);word(data+28,0x17000000);word(data+48,0x70000000);
    for(unsigned i=12;i<28;i++)data[i]=(unsigned char)i;
    memcpy(copy,data,sizeof(data));DrFaceVifStats stats;size_t bad;
    assert(!dr_face_vif_parse(data,sizeof(data),visit,&calls,&stats,&bad));
    assert(calls==6 && stats.commands==6 && stats.unpacks==1 && stats.vectors==1 && stats.continues==1 && stats.noops==4);
    assert(stats.guard_offset==48 && stats.dma_payload_bytes==32 && bad==SIZE_MAX && !memcmp(copy,data,64));
    for(size_t n=0;n<64;n++) {
        calls=0;memset(&stats,0xff,sizeof(stats));assert(dr_face_vif_parse(data,n,visit,&calls,&stats,&bad)<0);
        assert(!calls && !stats.commands);
    }
    word(data+40,0x4a000000);calls=0;
    assert(dr_face_vif_parse(data,64,visit,&calls,&stats,&bad)==DR_FACE_UNSUPPORTED && !calls && bad==40);
    word(data+40,0x6c020000);
    assert(dr_face_vif_parse(data,64,visit,&calls,&stats,&bad)==DR_FACE_TRUNCATED && !calls && bad==40);
    memcpy(data,copy,64);word(data+8,0x6c000000);
    assert(dr_face_vif_parse(data,64,NULL,NULL,&stats,&bad)==DR_FACE_TRUNCATED); /* NUM=0 means 256. */
    memcpy(data,copy,64);data[63]=1;
    assert(dr_face_vif_parse(data,64,visit,&calls,&stats,&bad)==DR_FACE_INVALID && !calls);
    memcpy(data,copy,64);word(data,0x30000002);
    assert(dr_face_vif_parse(data,64,visit,&calls,&stats,&bad)==DR_FACE_UNSUPPORTED && !calls);
    memcpy(data,copy,64);stop=1;
    assert(dr_face_vif_parse(data,64,visit,&calls,&stats,&bad)==DR_FACE_CALLBACK && bad==SIZE_MAX);
    assert(!stats.commands && !memcmp(data,copy,64));
    assert(dr_face_vif_parse(NULL,SIZE_MAX,NULL,NULL,&stats,&bad)==DR_FACE_INVALID);
    unsigned seed=360;
    for(unsigned i=0;i<10000;i++) {
        for(unsigned j=0;j<64;j++) {seed=seed*1664525u+1013904223u;data[j]=(unsigned char)(seed>>24);}
        dr_face_vif_parse(data,i%65,NULL,NULL,&stats,&bad);
    }
    puts("PASS: bounded face DMA/VIF, tag upper words, full preflight, guard/padding, truncation, NUM=0, unknown commands, callback failure, 10000 hostile blocks");
    return 0;
}
