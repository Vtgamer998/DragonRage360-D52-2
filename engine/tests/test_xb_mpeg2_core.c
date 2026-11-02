/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_mpeg2.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int calloc_fail,realloc_fail;
void *__real_calloc(size_t,size_t);void *__real_realloc(void *,size_t);
void *__wrap_calloc(size_t a,size_t b) {
    if(calloc_fail && !--calloc_fail)return NULL;
    return __real_calloc(a,b);
}
void *__wrap_realloc(void *p,size_t n) {
    if(realloc_fail && !--realloc_fail)return NULL;
    return __real_realloc(p,n);
}
typedef struct {const uint8_t *p;size_t n;int closed;} Input;
static int read_input(void *ctx,uint64_t at,void *p,size_t n) {
    Input *s=ctx;assert(at<=s->n && n<=s->n-at);
    memcpy(p,s->p+at,n);return 0;
}
static void close_input(void *ctx){((Input *)ctx)->closed++;}
static void check_error(const uint8_t *p,size_t n,uint8_t *frame,int expected) {
    Input s={p,n,0};XbMpegReader r={&s,n,read_input,close_input,NULL};
    XbMpeg *m=XbMpeg_OpenReader(&r);assert(m);
    memset(frame,0x5a,XB_MPEG_RGBA_BYTES);
    int result=XbMpeg_Next(m,frame,XB_MPEG_RGBA_BYTES);
    if(result!=expected)fprintf(stderr,"negative expected %d got %d n=%zu\n",expected,result,n);
    assert(result==expected && XbMpeg_Error(m)==expected);
    for(size_t i=0;i<XB_MPEG_RGBA_BYTES;i++)assert(frame[i]==0x5a);
    assert(XbMpeg_Next(m,frame,XB_MPEG_RGBA_BYTES)==expected);
    XbMpeg_Close(m);assert(s.closed==1);
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    size_t cap=2*1024*1024;uint8_t *p=malloc(cap),*changed=malloc(cap);
    uint8_t *frame=malloc(XB_MPEG_RGBA_BYTES);assert(p && changed && frame);
    size_t n=fread(p,1,cap,f);assert(n==cap);fclose(f);
    size_t seq=0,pic=0;
    for(size_t i=0;i+8<n;i++)if(!p[i] && !p[i+1] && p[i+2]==1) {
        if(p[i+3]==0xb3 && !seq)seq=i;
        if(p[i+3]==0 && !pic)pic=i;
    }
    assert(seq && pic);
    memcpy(changed,p,n);changed[seq+4]=0xff;
    check_error(changed,n,frame,XB_MPEG_FORMAT);
    memcpy(changed,p,n);changed[pic+5]=(changed[pic+5]&0xc7)|(2<<3);
    check_error(changed,n,frame,XB_MPEG_FORMAT);
    /* Every truncation before the first picture must be bounded and non-success. */
    for(size_t len=4;len<pic+64;len++) {
        Input s={p,len,0};XbMpegReader r={&s,len,read_input,close_input,NULL};
        XbMpeg *m=XbMpeg_OpenReader(&r);assert(m);
        assert(XbMpeg_Next(m,frame,XB_MPEG_RGBA_BYTES)<=0);XbMpeg_Close(m);assert(s.closed==1);
    }
    Input s={p,n,0};XbMpegReader r={&s,n,read_input,close_input,NULL};
    calloc_fail=1;assert(!XbMpeg_OpenReader(&r) && !s.closed);
    calloc_fail=2;check_error(p,n,frame,XB_MPEG_MEMORY);calloc_fail=0;
    realloc_fail=1;check_error(p,n,frame,XB_MPEG_MEMORY);realloc_fail=0;
    /* Valid PES syntax carrying >2 MiB without a picture: bounded ES cache. */
    size_t packet=65541,bigsize=packet*34;uint8_t *big=calloc(1,bigsize);assert(big);
    for(size_t i=0;i<bigsize;i+=packet) {
        big[i+2]=1;big[i+3]=0xe0;big[i+4]=255;big[i+5]=255;big[i+6]=0x80;
    }
    check_error(big,bigsize,frame,XB_MPEG_LIMIT);free(big);
    free(p);free(changed);free(frame);
    puts("PASS: real-PSS dimension/P-picture rejection, truncation sweep, allocation failures, 2 MiB ES cap and unchanged outputs");
    return 0;
}
