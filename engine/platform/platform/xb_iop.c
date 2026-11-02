/* SPDX-License-Identifier: MIT */
#include "xb_iop.h"
#include "xb_resconv.h"
#include <stddef.h>
#include <string.h>
#ifdef XB_HOST_TEST
#include <stdio.h>
#define LOG(...) ((void)fprintf(stderr,__VA_ARGS__))
#else
#include "xsdk/log.h"
#define LOG(...) xsdk_log_write(__VA_ARGS__)
#endif
extern void *Port_LowAlloc(size_t);
extern void Port_LowFree(void *);
static struct {void *ptr;size_t size;} regions[64];
static int last_error;
int XbIop_LastError(void){return last_error;}
const void *XbIop_View(uintptr_t address,size_t *available){
    if(!available)return NULL;
    *available=0;
    for(unsigned i=0;i<64;i++)if(regions[i].ptr){
        uintptr_t p=(uintptr_t)regions[i].ptr;
        if(address>=p && address-p<regions[i].size){*available=regions[i].size-(address-p);return (const void *)address;}
    }
    return NULL;
}
void *sceSifAllocIopHeap(int size){
    unsigned i;last_error=0;
    if(size<=0){last_error=-1;return NULL;}
    for(i=0;i<64;i++)if(!regions[i].ptr){
        void *p=Port_LowAlloc((size_t)size);
        if(!p){last_error=-2;return NULL;}
        regions[i].ptr=p;regions[i].size=(size_t)size;return p;
    }
    last_error=-2;return NULL;
}
int sceSifFreeIopHeap(void *ptr){
    unsigned i;
    if(!ptr)return 0;
    for(i=0;i<64;i++)if(regions[i].ptr==ptr){
        XbRes_Forget(ptr,regions[i].size);Port_LowFree(ptr);
        regions[i].ptr=NULL;regions[i].size=0;last_error=0;return 0;
    }
    last_error=-1;return -1;
}
int XbIop_Copy(void *dst,const void *src,int32_t size){
    unsigned i;uintptr_t d=(uintptr_t)dst;last_error=0;
    if(size<0 || (!src && size) || !dst){last_error=-1;return -1;}
    for(i=0;i<64;i++)if(regions[i].ptr){
        uintptr_t p=(uintptr_t)regions[i].ptr;
        if(d>=p && d-p<=regions[i].size && (size_t)size<=regions[i].size-(d-p)){
            if(size)memmove(dst,src,(size_t)size);
            return 0;
        }
    }
    last_error=-1;return -1;
}
void Dma_SendToIop(void *dst,void *src,int32_t size){
    int rc=XbIop_Copy(dst,src,size);
    if(rc)LOG("iop: rejected transfer size=%d; destination not an owned range",(int)size);
}
