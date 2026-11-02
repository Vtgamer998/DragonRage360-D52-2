/* SPDX-License-Identifier: MIT */
#include "xb_resconv.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
void XbRes_TestRead(int,int,uint64_t,uint64_t,void *,size_t);
static uint8_t buffers[515][16];
static int prepare(void *p,size_t n,int id){assert(p && n==16 && id>0);return 0;}
static int has(unsigned i,int expected){const void *base;size_t n;int id;int found=XbRes_ReadFile(buffers[i]+4,&base,&n,&id);if(found){assert(base==buffers[i] && n==16 && id==expected);}return found;}
int main(void){
 assert(!XbRes_RegisterChecked(1,1024,prepare,0));assert(!XbRes_RegisterChecked(1025,1025,prepare,1));
 assert(!XbRes_BeginCheckedReads());
 for(unsigned i=0;i<512;i++){XbRes_TestRead(1,(int)i,0,16,buffers[i],16);assert(!XbRes_LastError());assert(has(0,1));}
 for(unsigned i=0;i<512;i++)assert(has(i,(int)i+1));
 XbRes_Forget(buffers[7],16);assert(!has(7,8));
 XbRes_TestRead(1,512,0,16,buffers[512],16);assert(!XbRes_LastError() && has(512,513) && has(0,1));
 XbRes_TestRead(1,513,0,16,buffers[513],16);assert(XbRes_LastError()==-898 && !has(513,514));
 for(unsigned i=0;i<512;i++){if(i!=7)assert(has(i,(int)i+1));}
 assert(has(512,513));
 XbRes_EndCheckedReads();assert(!has(0,1) && !has(512,513));
 assert(!XbRes_BeginCheckedReads());buffers[0][0]=16;XbRes_TestRead(1,1024,0,16,buffers[0],16);XbRes_AfterBpe(buffers[0],buffers[1],16);assert(!XbRes_LastError() && has(1,1025));
 XbRes_Forget(buffers[1],16);assert(!has(1,1025));XbRes_EndCheckedReads();
 puts("PASS 512 live resource leases retained across reads; explicit revocation/reuse, bounded overflow without eviction, decoded ownership and session cleanup");
}
