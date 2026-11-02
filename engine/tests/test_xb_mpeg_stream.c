/* SPDX-License-Identifier: AGPL-3.0-or-later */
#define main iso_adapter_fixture_main
#include "test_xb_iso_file.c"
#undef main
#include "xb_mpeg.h"
#include "xb_mpeg2.h"
typedef struct {dr_span span;uint64_t at;uint8_t buffer[65537];size_t size,pos,chunk;unsigned callbacks,errors;} Feed;
static int feed(void *mp,void *data,void *opaque) {
    (void)data;Feed *f=opaque;
    assert(sceMpegReset(mp)==XB_MPEG_ARGUMENT && sceMpegDelete(mp)==XB_MPEG_ARGUMENT);
    for(unsigned tries=0;tries<128;tries++) {
        if(f->pos==f->size) {
            if(f->at==f->span.size){assert(!XbSonyMpeg_End(mp));return 1;}
            size_t n=f->chunk;if((uint64_t)n>f->span.size-f->at)n=(size_t)(f->span.size-f->at);
            assert(!XbDisc_ReadSpan(&f->span,f->at,f->buffer,n));f->at+=n;f->size=n;f->pos=0;
        }
        int n=sceMpegDemuxPss(mp,f->buffer+f->pos,(int)(f->size-f->pos));
        assert(n>=0);f->pos+=(size_t)n;if(!n)return 1;
    }
    return 1;
}
static int video(void *mp,void *data,void *opaque) {
    Feed *f=opaque;XbMpegStreamData *s=data;
    assert(s && s->type==0 && s->len<=65535 && XbSonyMpeg_CanQueue(mp,s->len));
    assert(sceMpegDelete(mp)==XB_MPEG_ARGUMENT);f->callbacks++;return 1;
}
static int error(void *mp,void *data,void *opaque){(void)mp;assert(data);((Feed *)opaque)->errors++;return 0;}
int main(int argc,char **argv) {
    assert(argc==5);image_path=argv[1];int count=atoi(argv[3]);size_t chunk=(size_t)atoi(argv[4]);assert(chunk && chunk<=65537);
    assert(!XbDisc_Mount("game:/test.iso"));Feed f={0};f.chunk=chunk;assert(!XbDisc_FindFile(argv[2],&f.span));
    XbMpegObject object;uint8_t work[128];assert(!sceMpegInit() && !sceMpegCreate(&object,work,sizeof(work)));
    assert(sceMpegCreate(&object,work,sizeof(work))<0);
    sceMpegAddCallback(&object,1,feed,&f);sceMpegAddCallback(&object,0,error,&f);sceMpegAddStrCallback(&object,0,0,video,&f);
    XbMpeg *reference=XbMpeg_Open(argv[2]);assert(reference);
    uint8_t *linear=malloc(XB_MPEG_RGBA_BYTES),*blocks=malloc(XB_MPEG_RGBA_BYTES);assert(linear && blocks);
    for(int i=0;i<count;i++) {
        int n=2;unsigned tries=0;
        while(n==2 && ++tries<1024)n=sceMpegGetPicture(&object,blocks,896);
        if(n)fprintf(stderr,"frame %d result %d decoder %d\n",i,n,XbSonyMpeg_Error(&object));
        assert(n==0 && !sceMpegIsEnd(&object) && object.frameCount==i+1 && object.pts==(int64_t)i*3003);
        assert(XbMpeg_Next(reference,linear,XB_MPEG_RGBA_BYTES)==1);
        for(unsigned x=0;x<32;x++)for(unsigned y=0;y<28;y++)for(unsigned row=0;row<16;row++)for(unsigned col=0;col<16;col++) {
            const uint8_t *a=linear+((y*16+row)*512+x*16+col)*4;
            const uint8_t *b=blocks+((x*28+y)*256+row*16+col)*4;
            assert(!memcmp(a,b,3) && b[3]==0x80);
        }
    }
    if(count>=2810) {
        assert(sceMpegGetPicture(&object,blocks,896)==-1 && sceMpegIsEnd(&object));
        assert(XbMpeg_Next(reference,linear,XB_MPEG_RGBA_BYTES)==0);
    }
    assert(!f.errors && f.callbacks>0);
    assert(!sceMpegReset(&object) && !sceMpegIsEnd(&object));
    uint8_t invalid[4]={9,8,7,6};assert(sceMpegDemuxPss(&object,invalid,4)==XB_MPEG_FORMAT && f.errors==1);
    assert(sceMpegIsEnd(&object));memset(blocks,0xa5,XB_MPEG_RGBA_BYTES);
    assert(sceMpegGetPicture(&object,blocks,896)==XB_MPEG_FORMAT);
    for(size_t i=0;i<XB_MPEG_RGBA_BYTES;i++)assert(blocks[i]==0xa5);
    assert(!sceMpegDelete(&object) && sceMpegDelete(&object)<0);
    XbMpeg_Close(reference);XbDisc_Unmount();free(linear);free(blocks);
    printf("PASS: %d actual ISO movie frames, PSS chunks %zu, 9 APIs, RGB identical to pull decoder, macroblock ordering/alpha, callbacks/reset/end and reentry refusal\n",count,chunk);
    return 0;
}
