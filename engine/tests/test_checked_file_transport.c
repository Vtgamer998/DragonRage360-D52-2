/* SPDX-License-Identifier: MIT */
#include "xb_resconv.h"
#include "xb_disc.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
void XbRes_TestRead(int,int,uint64_t,uint64_t,void*,size_t);
extern void (*gXbFilePostRead)(int,int,uint64_t,uint64_t,void *,size_t);
int ADXF_LoadPartitionNw(int,char *,void *,void *);
void *ADXF_OpenAfs(int,int);
int ADXF_ReadNw(void *,int,void *);
int ADXF_GetStat(void *);
void ADXF_Close(void *);
void *Port_LowAlloc(size_t n){return calloc(1,n);}
void Port_LowFree(void *p){free(p);}
void Port_VBlank(void){}
void xsdk_log_write(const char *fmt,...){(void)fmt;}
static const unsigned char source[8]={1,2,3,4,5,6,7,8};
int XbDisc_FindFile(const char *name,dr_span *out){assert(strstr(name,"pzs3us1/")==name);*out=(dr_span){.offset=0,.size=8,.directory=0};return 0;}
int XbDisc_ReadSpan(const dr_span *span,uint64_t off,void *out,size_t n){assert(span->size==8 && off<=8 && n<=8-off);memcpy(out,source+off,n);return 0;}
static int converter(void *out,size_t n,int id){assert(n==8 && id==100);((unsigned char *)out)[0]=99;return 0;}
static int read_file(int index,unsigned char *out,int *status){void *h=ADXF_OpenAfs(1,index);assert(h);int n=ADXF_ReadNw(h,1,out);*status=ADXF_GetStat(h);ADXF_Close(h);return n;}
int main(void){
 unsigned char out[2048];int status;
 assert(!ADXF_LoadPartitionNw(1,"PZS3US1.AFS",NULL,NULL));gXbFilePostRead=XbRes_TestRead;
 assert(!XbRes_RegisterChecked(100,100,converter,0));assert(!XbRes_BeginCheckedReads());
 assert(read_file(99,out,&status)==1 && status==3 && out[0]==99 && !XbRes_LastError());
 assert(read_file(499,out,&status)==0 && status==4 && XbRes_LastError()<0);
 assert(read_file(99,out,&status)==0 && status==4 && out[0]==1);
 XbRes_EndCheckedReads();assert(!XbRes_BeginCheckedReads());assert(read_file(99,out,&status)==1 && status==3 && out[0]==99);XbRes_EndCheckedReads();
 puts("PASS actual ADXF transport: converter success, unknown-file STAT_ERROR, persistent rejection, fresh-scope recovery");return 0;
}
