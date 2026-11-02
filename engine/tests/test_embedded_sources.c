/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_resconv.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
void XbRes_TestRead(int,int,uint64_t,uint64_t,void *,size_t);
static int decoded_calls;
static int prepare(void *p,size_t n,int id){assert(p && n==512 && id==300);return 0;}
static int decoded(void *p,size_t n,int id){assert(p && n==8 && id==300);decoded_calls++;return 0;}
static int resolve(const void *base,size_t n,const void *p,size_t *span,XbResCheckedFn *fn){assert(n==512);if(p!=(const unsigned char *)base+16)return -904;*span=300;*fn=decoded;return 0;}
int main(void){
 XbResContainerRange duplicate[2]={{300,300,prepare,resolve},{300,300,prepare,resolve}};
 assert(XbRes_RegisterContainerBatch(duplicate,2)<0 && !XbRes_HasCheckedConverter(300));
 assert(!XbRes_RegisterContainer(300,300,prepare,resolve));
 XbResContainerRange bad[2]={{301,301,prepare,resolve},{300,300,prepare,resolve}};
 assert(XbRes_RegisterContainerBatch(bad,2)<0 && !XbRes_HasCheckedConverter(301));
 unsigned char parent[512]={0},out[8]={0};parent[16]=8;
 XbRes_TestRead(1,299,0,sizeof(parent),parent,sizeof(parent));assert(XbRes_BeginCheckedReads()<0);XbRes_Forget(parent,sizeof(parent));
 assert(!XbRes_BeginCheckedReads());assert(XbRes_RegisterContainerBatch(bad,1)<0 && !XbRes_HasCheckedConverter(301));
 XbRes_TestRead(1,299,0,sizeof(parent),parent,sizeof(parent));assert(XbRes_BpeSourceSize(parent+16)==300);
 XbRes_AfterBpe(parent+16,out,8);XbRes_AfterBpe(parent+16,out,8);assert(decoded_calls==2 && !XbRes_LastError());
 assert(!XbRes_BpeSourceSize(parent+20) && XbRes_LastError()==-904);XbRes_EndCheckedReads();
 assert(!XbRes_BeginCheckedReads());XbRes_TestRead(1,299,0,sizeof(parent),parent,sizeof(parent));XbRes_Forget(parent,sizeof(parent));assert(!XbRes_BpeSourceSize(parent+16) && XbRes_LastError());XbRes_EndCheckedReads();
 puts("PASS embedded source registry: atomic container registration, exact offsets/extents, repeated decode, sticky refusal, fresh-session guard, invalidation on free");return 0;
}
