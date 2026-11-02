/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Synthetic reads through the actual registry and bounded BPE implementation.
 * Heap fixtures are for the test executable only; not game/platform stubs.
 */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "xb_resconv.h"
#include "xb_demo_resources.h"
void XbRes_TestRead(int,int,uint64_t,uint64_t,void*,size_t);
void *XbBpe_Decode(void*,void*,int32_t*);
void *Heap_Alloc(int32_t n,int32_t align,int32_t tail,int32_t heap) {
    assert(n>=0 && align==0x20 && tail==0 && heap==2);return malloc((size_t)n);
}
void Heap_Free(void *p){free(p);}
static int calls;
static int swap(void *p,size_t n,int id){assert(id==6 || id==21);assert(n==8);calls++;XbRes_Swap32(p,2);return 0;}
static int reject(void *p,size_t n,int id){(void)p;(void)n;(void)id;return -77;}
static size_t packed(uint8_t *pk){
    size_t n=8;pk[n++]=127;for(unsigned i=0;i<128;i++)pk[n++]=(uint8_t)i;
    pk[n++]=127;for(unsigned i=128;i<256;i++)pk[n++]=(uint8_t)i;
    pk[n++]=0;pk[n++]=8;
    for(unsigned i=1;i<=8;i++)pk[n++]=(uint8_t)i;
    uint32_t raw=8,pack=(uint32_t)n-8;
    for(unsigned i=0;i<4;i++){pk[i]=(uint8_t)(raw>>(8*i));pk[4+i]=(uint8_t)(pack>>(8*i));}return n;
}
int main(void){
    uint8_t pk[512],data[8]={1,2,3,4,5,6,7,8},old[8];size_t n=packed(pk);int32_t raw=-1;
    assert(!XbRes_RegisterChecked(6,6,swap,1));assert(!XbRes_RegisterChecked(21,21,swap,0));
    assert(!XbRes_RegisterChecked(40,40,reject,0));
    assert(XbRes_RegisterChecked(6,10,swap,0)==-1);
    XbRes_TestRead(1,5,0,n,pk,n);assert(calls==0);
    uint8_t *out=XbBpe_Decode(pk,0,&raw);assert(out && raw==8 && calls==1);
    assert(!memcmp(out,(uint8_t[]){4,3,2,1,8,7,6,5},8));free(out);
    memcpy(old,data,8);XbRes_TestRead(1,20,0,16,data,8);assert(calls==1 && !memcmp(old,data,8));
    XbRes_TestRead(1,20,0,8,data,8);assert(calls==2 && data[0]==4);
    XbRes_TestRead(1,39,0,8,data,8);assert(XbRes_LastError()==-77);
    /* A subsequent read overlapping the source invalidates the packed id. */
    XbRes_TestRead(1,600,0,n,pk,n);out=XbBpe_Decode(pk,0,&raw);
    assert(out && raw==8 && out[0]==1 && calls==2);free(out);
    assert(XbBpe_Decode(data,0,&raw)==NULL && raw==0);
    /* Malformed packed length: reject before allocating or decoding. */
    pk[4]=0xff;pk[5]=0xff;assert(XbBpe_Decode(pk,0,&raw)==NULL);
    uint8_t anim[0x94]={0};assert(!XbRes_RegisterAnimation(50,50,0));
    XbRes_TestRead(1,49,0,sizeof(anim),anim,sizeof(anim));assert(!XbRes_LastError());
    XbRes_TestRead(1,49,0,8,data,8);assert(XbRes_LastError()!=0);
    uint8_t bone[64]={0};bone[6]=1;assert(!XbRes_RegisterSkeleton(51,51,0));
    XbRes_TestRead(1,50,0,64,bone,64);assert(!XbRes_LastError());
    bone[6]=0;XbRes_TestRead(1,50,0,64,bone,64);assert(XbRes_LastError()!=0);
    uint8_t plan[8]={1,2,3,4,5,6,7,8};
    assert(!XbRes_SwapPlan(plan,8,"I2"));assert(plan[0]==4 && plan[4]==8);
    assert(XbRes_SwapPlan(plan,8,"Q184467440737095516159")==-1);
    assert(XbRes_SwapPlan(plan,8,"I0*")==-1);
    /* Logging must stay once-per-id beyond the old fixed 512-entry limit. */
    for(int repeat=0;repeat<2;repeat++)for(int id=1000;id<1700;id++)XbRes_TestRead(1,id-1,0,8,data,8);
    assert(XbRes_Id(1,INT32_MAX)==INT32_MIN);assert(XbRes_Id(0,-1)==INT32_MIN);
    char mapPath[]="/tmp/dr360-map-XXXXXX";int fd=mkstemp(mapPath);assert(fd>=0);
    FILE *mf=fdopen(fd,"w");assert(mf);fputs("animation 90 90 0\n",mf);assert(!fclose(mf));
    assert(XbRes_LoadMap(mapPath)==1);
    mf=fopen(mapPath,"w");assert(mf);fputs("animation 91 91 0\nskeleton 999999999999999999999 93 0\n",mf);fclose(mf);
    assert(XbRes_LoadMap(mapPath)==-1);
    /* Invalid map must not install its valid first row. */
    assert(!XbRes_RegisterAnimation(91,91,0));assert(!unlink(mapPath));
    XbRes_Report();puts("PASS: read/converter/BPE/adapters/errors/logging pipeline (synthetic, host)");return 0;
}
