/* SPDX-License-Identifier: MIT
 * IO fixtures validate ADXF's real file/hook path without an Xbox or ISO.
 */
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "xb_file.h"
#include "xb_disc.h"
#include "xb_resconv.h"
void *Port_LowAlloc(size_t n){return calloc(1,n);}
void Port_LowFree(void *p){free(p);}
void Port_VBlank(void){}
void xsdk_log_write(const char *format,...){(void)format;}
static uint8_t disk[3000];static int calls,reject;
int XbDisc_FindFile(const char *name,dr_span *out){
    assert(!strcmp(name,"pzs3us1/00000.bin"));memset(out,0,sizeof(*out));out->size=sizeof(disk);return DR_OK;
}
int XbDisc_ReadSpan(const dr_span *s,uint64_t off,void *buf,size_t n){
    assert(s->size==sizeof(disk) && off<=s->size && n<=s->size-off);memcpy(buf,disk+off,n);return DR_OK;
}
static void hook(int pt,int id,uint64_t off,uint64_t size,void *buf,size_t n){
    assert(pt==1 && id==0 && size==sizeof(disk));assert(!memcmp(buf,disk+off,n));
    if(calls==0)assert(off==0 && n==sizeof(disk));
    else if(calls==1)assert(off==0 && n==2048);
    else if(calls==2)assert(off==2048 && n==952);
    calls++;
}
static int reject_conversion(void *p,size_t n,int id){(void)p;(void)n;assert(id==1);return reject?-17:0;}
int main(void){
    uint8_t buf[4096];for(unsigned i=0;i<sizeof(disk);i++)disk[i]=(uint8_t)i;
    assert(!ADXF_LoadPartitionNw(1,"PZS3US1.AFS",0,0));gXbFilePostRead=hook;
    void *h=ADXF_OpenAfs(1,0);assert(h && ADXF_GetFsizeSct(h)==2);
    memset(buf,0xff,sizeof(buf));assert(ADXF_ReadNw(h,2,buf)==2);assert(calls==1);
    for(unsigned i=3000;i<4096;i++)assert(buf[i]==0);ADXF_Close(h);
    h=ADXF_OpenAfs(1,0);assert(ADXF_ReadNw(h,1,buf)==1);assert(ADXF_ReadNw(h,1,buf)==1);ADXF_Close(h);assert(calls==3);
    XbRes_Install();assert(!XbRes_RegisterChecked(1,1,reject_conversion,0));reject=1;
    h=ADXF_OpenAfs(1,0);assert(ADXF_ReadNw(h,2,buf)==0 && ADXF_GetStat(h)==4);ADXF_Close(h);
    assert(ADXF_LoadPartitionNw(8,"PZS3US1.AFS",0,0)==-1);
    assert(ADXF_ReadNw(NULL,1,buf)==0);
    puts("PASS: actual ADXF full/partial reads, offsets, valid lengths, padding, converter rejection");return 0;
}
