/* SPDX-License-Identifier: MIT */
#include "xb_resconv.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
void XbRes_TestRead(int,int,uint64_t,uint64_t,void*,size_t);
static unsigned calls;
static int checked(void *buf,size_t n,int id){assert(n==8 && (id==100 || id==102));calls++;uint32_t v=XbRes_Le32(buf);memcpy(buf,&v,4);return 0;}
static int rejected(void *buf,size_t n,int id){(void)buf;(void)n;assert(id==103);return -23;}
static void legacy(void *buf,size_t n,int id){(void)buf;(void)n;(void)id;assert(!"unchecked callback entered strict mode");}
static void read_id(int id,void *b){XbRes_TestRead(1,id-1,0,8,b,8);}
int main(void){
 uint8_t data[8]={8},out[8]={1};assert(!XbRes_RegisterChecked(100,100,checked,0));assert(!XbRes_Register(101,101,legacy,0));assert(!XbRes_RegisterChecked(102,102,checked,1));assert(!XbRes_RegisterChecked(103,103,rejected,0));
 XbResCheckedRange batch[2]={{200,200,checked,0},{102,102,checked,0}};
 assert(XbRes_RegisterCheckedBatch(batch,2)<0 && !XbRes_HasCheckedConverter(200));
 batch[1].first=batch[1].last=200;assert(XbRes_RegisterCheckedBatch(batch,2)<0 && !XbRes_HasCheckedConverter(200));
 batch[1].first=batch[1].last=201;batch[1].packed=2;assert(XbRes_RegisterCheckedBatch(batch,2)<0 && !XbRes_HasCheckedConverter(200));
 batch[1].packed=0;assert(!XbRes_BeginCheckedReads());assert(XbRes_RegisterCheckedBatch(batch,2)<0 && !XbRes_HasCheckedConverter(200));XbRes_EndCheckedReads();
 assert(!XbRes_RegisterCheckedBatch(batch,2) && XbRes_HasCheckedConverter(200) && XbRes_HasCheckedConverter(201));
 assert(XbRes_RegisterCheckedBatch(NULL,0)<0 && XbRes_RegisterCheckedBatch(batch,65)<0);
 assert(XbRes_HasCheckedConverter(100) && !XbRes_HasCheckedConverter(101) && !XbRes_HasCheckedConverter(999));
 assert(!XbRes_BeginCheckedReads());assert(XbRes_BeginCheckedReads()<0);read_id(100,data);assert(!XbRes_LastError() && calls==1);
 read_id(999,data);int error=XbRes_LastError();assert(error<0);read_id(100,data);assert(XbRes_LastError()==error && calls==1);XbRes_EndCheckedReads();
 assert(!XbRes_BeginCheckedReads());read_id(101,data);assert(XbRes_LastError()<0);XbRes_EndCheckedReads();
 assert(!XbRes_BeginCheckedReads());XbRes_TestRead(1,99,4,12,data,8);assert(XbRes_LastError()<0 && calls==1);XbRes_EndCheckedReads();
 assert(!XbRes_BeginCheckedReads());read_id(102,data);assert(!XbRes_LastError());XbRes_AfterBpe(data,out,8);assert(!XbRes_LastError() && calls==2);XbRes_AfterBpe(data,out,8);assert(!XbRes_LastError() && calls==3);uint8_t unknown[8]={8};XbRes_AfterBpe(unknown,out,8);assert(XbRes_LastError()<0 && calls==3);XbRes_EndCheckedReads();
 assert(!XbRes_BeginCheckedReads());read_id(103,data);assert(XbRes_LastError()==-23);read_id(100,data);assert(XbRes_LastError()==-23 && calls==3);XbRes_EndCheckedReads();
 uint8_t packed[33][8];memset(packed,0,sizeof(packed));for(unsigned i=0;i<33;i++)packed[i][0]=8;
 assert(!XbRes_BeginCheckedReads());for(unsigned i=0;i<32;i++){read_id(102,packed[i]);assert(!XbRes_LastError());}read_id(102,packed[32]);assert(XbRes_LastError()<0);XbRes_EndCheckedReads();
 read_id(102,data);assert(!XbRes_LastError());assert(XbRes_BeginCheckedReads()<0);XbRes_Forget(data,8);assert(!XbRes_BeginCheckedReads());XbRes_EndCheckedReads();
 puts("PASS checked reads: coverage, legacy/partial rejection, sticky failure, BPE source tracking, tracker exhaustion, fresh-session guard");return 0;
}
