/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_motion_pack.h"
#include "xb_resconv.h"
#include "dr_container.h"
#include <stdint.h>
#ifdef malloc
#undef malloc /* Scratch belongs to libc, never the game's one-shot arena. */
#endif
#include <stdlib.h>
#include <string.h>
enum {MIN_MOTIONS=414,MAX_MOTIONS=512,BAD=-904,MEMORY=-905};
static uint32_t native(const void *p){uint32_t v;memcpy(&v,p,4);return v;}
static int table(const uint8_t *p,size_t n,int cpu){
    if(!p || n<8 || n>0x160800)return BAD;
    unsigned count=cpu?native(p):XbRes_Le32(p),table_bytes=(count+2)*4;
    if(count<MIN_MOTIONS || count>MAX_MOTIONS || table_bytes>n)return BAD;
    uint32_t previous=0,previous_word=0;
    for(unsigned i=0;i<=count;i++){
        uint32_t word=cpu?native(p+4+i*4):XbRes_Le32(p+4+i*4),at=word&~3u;
        if(at<table_bytes || at>n || (i && at<previous) || (i && at==previous && word!=previous_word))return BAD;
        previous=at;previous_word=word;
    }
    return 0;
}
int XbMotionPack_Prepare(void *source,size_t n,int be){
    uint8_t *p=source;
    if((be!=0 && be!=1) || table(p,n,0))return BAD;
    void *scratch=malloc(0xc000);if(!scratch)return MEMORY;
    int result=0;
    unsigned count=XbRes_Le32(p);
    for(unsigned i=0;i<count;i++){
        uint32_t a=XbRes_Le32(p+4+i*4)&~3u,z=XbRes_Le32(p+8+i*4)&~3u;
        if(a==z)continue;
        size_t written=0;
        int e=dr_motion_from_memory(p+a,z-a,1,0,scratch,0xc000,&written);
        if(e && e!=DR_LOAD_EMPTY){result=e;break;}
    }
    free(scratch);if(result)return result;
    if(be)XbRes_Swap32(p,count+2);
    return 0;
}
static int decoded(void *p,size_t n,int id){
    (void)id;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__==__ORDER_BIG_ENDIAN__
    return dr_anim_prepare(p,n,1);
#else
    return dr_anim_prepare(p,n,0);
#endif
}
static int prepare(void *p,size_t n,int id){
    if(id!=0x598 && id!=0x5a2)return BAD;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__==__ORDER_BIG_ENDIAN__
    return XbMotionPack_Prepare(p,n,1);
#else
    return XbMotionPack_Prepare(p,n,0);
#endif
}
static int resolve(const void *base,size_t n,const void *source,size_t *extent,XbResCheckedFn *fn){
    const uint8_t *p=base;uintptr_t a=(uintptr_t)base,b=(uintptr_t)source;
    if(!extent || !fn || b<a || b-a>=n || table(p,n,1))return BAD;
    unsigned count=native(p);
    for(unsigned i=0;i<count;i++){
        uint32_t at=native(p+4+i*4)&~3u,end=native(p+8+i*4)&~3u;
        if(at==b-a && end>at){*extent=end-at;*fn=decoded;return 0;}
    }
    return BAD;
}
int XbMotionPack_RegisterInitial(void){
    static const XbResContainerRange ranges[]={
        {0x598,0x598,prepare,resolve},{0x5a2,0x5a2,prepare,resolve}
    };
    return XbRes_RegisterContainerBatch(ranges,2);
}
