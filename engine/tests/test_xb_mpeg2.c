/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Reuse only NT transport fixture; actual ISO reader and native movie adapter.
 */
#define main iso_adapter_fixture_main
#include "test_xb_iso_file.c"
#undef main
#include "xb_mpeg2.h"
static void extract(const char *path,const char *out) {
    dr_span s;assert(!XbDisc_FindFile(path,&s));
    FILE *f=fopen(out,"wb");assert(f);uint8_t b[65536];
    for(uint64_t at=0;at<s.size;) {
        size_t n=sizeof(b);if((uint64_t)n>s.size-at)n=(size_t)(s.size-at);
        assert(!XbDisc_ReadSpan(&s,at,b,n) && fwrite(b,1,n,f)==n);at+=n;
    }
    assert(!fclose(f));
}
typedef struct {const uint8_t *data;size_t size;int reads,closed,fail;} Memory;
static int mem_read(void *ctx,uint64_t at,void *p,size_t n) {
    Memory *s=ctx;s->reads++;
    assert(at<=s->size && n<=s->size-at);
    if(s->fail)return -1;
    memcpy(p,s->data+at,n);return 0;
}
static void mem_close(void *ctx){((Memory *)ctx)->closed++;}
static void rejects(uint8_t *frame) {
    const uint8_t bad[]={0,0,1,0xe0,0,6,0x80,0,100,1,2,3};
    Memory s={bad,sizeof(bad),0,0,0};XbMpegReader r={&s,s.size,mem_read,mem_close,NULL};
    assert(!XbMpeg_OpenReader(NULL));r.read=NULL;assert(!XbMpeg_OpenReader(&r));
    r.read=mem_read;XbMpeg *m=XbMpeg_OpenReader(&r);assert(m);
    memset(frame,0xa5,XB_MPEG_RGBA_BYTES);
    assert(XbMpeg_Next(m,frame,1)==XB_MPEG_ARGUMENT && !s.reads);
    assert(XbMpeg_Next(m,frame,XB_MPEG_RGBA_BYTES)==XB_MPEG_FORMAT);
    int reads=s.reads;
    assert(XbMpeg_Next(m,frame,XB_MPEG_RGBA_BYTES)==XB_MPEG_FORMAT && s.reads==reads);
    for(size_t i=0;i<XB_MPEG_RGBA_BYTES;i++)assert(frame[i]==0xa5);
    XbMpeg_Close(m);assert(s.closed==1);
    s.fail=1;m=XbMpeg_OpenReader(&r);assert(m);
    assert(XbMpeg_Next(m,frame,XB_MPEG_RGBA_BYTES)==XB_MPEG_IO);
    XbMpeg_Close(m);assert(s.closed==2);
}
int main(int argc,char **argv) {
    assert(argc==6 || argc==7);image_path=argv[1];int limit=atoi(argv[5]);assert(limit>0);
    uint8_t *frame=malloc(XB_MPEG_RGBA_BYTES);assert(frame);rejects(frame);
    assert(!XbMpeg_Open(argv[2]));assert(!XbDisc_Mount("game:/test.iso"));
    assert(!XbMpeg_Open("disc/absent.PSS"));
    extract(argv[2],argv[3]);XbMpeg *m=XbMpeg_Open(argv[2]);assert(m);
    FILE *f=fopen(argv[4],"wb");assert(f);
    for(int i=0;i<limit;i++) {
        int n=XbMpeg_Next(m,frame,XB_MPEG_RGBA_BYTES);
        if(n!=1)fprintf(stderr,"frame %d result %d\n",i,n);
        assert(n==1 && !XbMpeg_Error(m));
        if(i<32)assert(fwrite(frame,1,XB_MPEG_RGBA_BYTES,f)==XB_MPEG_RGBA_BYTES);
    }
    if(argc==7){assert(!strcmp(argv[6],"--expect-eof"));assert(XbMpeg_Next(m,frame,XB_MPEG_RGBA_BYTES)==0);}
    assert(!fclose(f));
    /* A remount invalidates even buffered movie bytes at the next frame. */
    XbDisc_Unmount();memset(frame,0xa5,XB_MPEG_RGBA_BYTES);
    assert(XbMpeg_Next(m,frame,XB_MPEG_RGBA_BYTES)==XB_MPEG_IO);
    for(size_t i=0;i<XB_MPEG_RGBA_BYTES;i++)assert(frame[i]==0xa5);
    assert(!XbDisc_Mount("game:/test.iso"));
    assert(XbMpeg_Next(m,frame,XB_MPEG_RGBA_BYTES)==XB_MPEG_IO);
    XbMpeg_Close(m);XbDisc_Unmount();free(frame);
    puts("PASS: real mounted ISO/PSS CPU decode; invalid PES, IO, bounds, sticky errors, ownership and stale mounts; NT transport mocked; no GPU/console execution");
    printf("Decoded %d frames; retained first %d RGBA frames for comparison\n",limit,limit<32?limit:32);
    return 0;
}
