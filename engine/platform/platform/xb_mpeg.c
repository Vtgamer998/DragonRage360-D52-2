/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Real streaming libmpeg subset used by BT3; no MIPS/IPU, no path side channel.
 */
#include "xb_mpeg.h"
#include "xb_mpeg2.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#if UINTPTR_MAX==UINT32_MAX
_Static_assert(sizeof(XbMpegObject)==0x48,"sceMpeg size");
_Static_assert(offsetof(XbMpegObject,sys)==0x40,"sceMpeg sys");
#endif
typedef struct {XbMpegCallback call;void *data;} Callback;
typedef struct {
    XbMpegObject *object;XbMpeg *decoder;uint8_t *rgba;
    uint8_t packet[65541+8];size_t have,need;
    Callback cb[5],video;int error,ended,drained,decoding,demuxing,notifying;
} State;
static State states[8];
static State *get(void *object){for(unsigned i=0;i<8;i++)if(object && states[i].object==object)return states+i;return NULL;}
static int callback(State *s,Callback cb,void *data) {
    if(!cb.call)return 1;
    s->notifying++;int result=cb.call(s->object,data,cb.data);s->notifying--;return result;
}
static int fail(State *s,int e) {
    if(!s->error){s->error=e;s->object->flags|=1;(void)callback(s,s->cb[0],&s->error);}
    return s->error;
}
static int busy(const State *s){return s->decoding || s->demuxing || s->notifying;}
int32_t sceMpegInit(void) {
    for(unsigned i=0;i<8;i++)if(states[i].object && busy(states+i))return XB_MPEG_ARGUMENT;
    for(unsigned i=0;i<8;i++)if(states[i].object)sceMpegDelete(states[i].object);
    return 0;
}
int32_t sceMpegCreate(void *mp,uint8_t *work,int32_t size) {
    if(!mp || !work || size<128 || get(mp))return XB_MPEG_ARGUMENT;
    State *s=NULL;for(unsigned i=0;i<8;i++)if(!states[i].object){s=states+i;break;}
    if(!s)return XB_MPEG_LIMIT;
    XbMpeg *decoder=XbMpeg_OpenStream();uint8_t *rgba=malloc(XB_MPEG_RGBA_BYTES);
    if(!decoder || !rgba){XbMpeg_Close(decoder);free(rgba);return XB_MPEG_MEMORY;}
    memset(mp,0,sizeof(XbMpegObject));memset(s,0,sizeof(*s));
    s->object=mp;s->decoder=decoder;s->rgba=rgba;s->need=4;s->object->sys=s;
    return 0;
}
int32_t sceMpegDelete(void *mp) {
    State *s=get(mp);if(!s || busy(s))return XB_MPEG_ARGUMENT;
    XbMpeg_Close(s->decoder);free(s->rgba);memset(mp,0,sizeof(XbMpegObject));memset(s,0,sizeof(*s));return 0;
}
int32_t sceMpegReset(void *mp) {
    State *s=get(mp);if(!s || busy(s))return XB_MPEG_ARGUMENT;
    XbMpeg *decoder=XbMpeg_OpenStream();if(!decoder)return fail(s,XB_MPEG_MEMORY);
    XbMpeg_Close(s->decoder);s->decoder=decoder;s->error=s->ended=s->drained=0;s->have=0;s->need=4;
    memset(mp,0,sizeof(XbMpegObject));s->object->sys=s;return 0;
}
void *sceMpegAddCallback(void *mp,int32_t type,XbMpegCallback cb,void *data) {
    State *s=get(mp);if(!s || busy(s) || (type!=0 && type!=1 && type!=4))return NULL;
    void *old=(void *)s->cb[type].call;s->cb[type]=(Callback){cb,data};return old;
}
void *sceMpegAddStrCallback(void *mp,int32_t type,int32_t ch,XbMpegCallback cb,void *data) {
    State *s=get(mp);if(!s || busy(s) || type || ch)return NULL;
    void *old=(void *)s->video.call;s->video=(Callback){cb,data};return old;
}
int XbSonyMpeg_CanQueue(void *mp,uint32_t bytes){State *s=get(mp);return s && !s->error && !s->ended && bytes<=XbMpeg_StreamSpace(s->decoder);}
int XbSonyMpeg_Error(void *mp){State *s=get(mp);return s?s->error:XB_MPEG_ARGUMENT;}
/* 1 delivered, 0 backpressure, negative error. packet includes full PES header. */
static int deliver(State *s) {
    uint8_t *p=s->packet;unsigned code=p[3];
    if(code==0xba || code==0xbb || code==0xbe || code==0xbd || code==0xbf)return 1;
    if(code==0xb9){s->ended=1;return XbMpeg_EndStream(s->decoder)?fail(s,XB_MPEG_FORMAT):1;}
    if(code<0xe0 || code>0xef)return 1; /* other complete non-video PES */
    if(code!=0xe0 || s->need<9 || (p[6]&0xc0)!=0x80)return fail(s,XB_MPEG_FORMAT);
    size_t off=9+(size_t)p[8];if(off>s->need)return fail(s,XB_MPEG_FORMAT);
    size_t len=s->need-off;if(!XbSonyMpeg_CanQueue(s->object,(uint32_t)len))return 0;
    XbMpegStreamData str={0,p+6,p+off,(uint32_t)len,-1,-1};
    if(s->video.call && !callback(s,s->video,&str))return 0;
    int e=XbMpeg_PushES(s->decoder,p+off,len);return e?fail(s,e):1;
}
int32_t sceMpegDemuxPss(void *mp,uint8_t *bytes,int32_t size) {
    State *s=get(mp);if(!s || size<0 || (!bytes && size) || s->demuxing)return XB_MPEG_ARGUMENT;
    if(s->error)return s->error;
    if(s->ended)return 0;
    s->demuxing=1;size_t at=0;
    while(!s->error && !s->ended) {
        if(s->have==s->need && s->need>=4) {
            uint8_t *p=s->packet;
            if(s->need==4) {
                if(p[0] || p[1] || p[2]!=1){fail(s,XB_MPEG_FORMAT);break;}
                s->need=p[3]==0xba?14:p[3]==0xb9?4:6;
            } else if(s->need==6 && p[3]!=0xba) {
                s->need=6+(size_t)p[4]*256+p[5];
                if(s->need==6 && p[3]>=0xe0 && p[3]<=0xef){fail(s,XB_MPEG_FORMAT);break;}
            } else if(s->need==14 && p[3]==0xba) {
                if((p[4]&0xc0)!=0x40){fail(s,XB_MPEG_FORMAT);break;}
                s->need=14+(p[13]&7);
            }
            if(s->have==s->need) {
                int e=deliver(s);if(e<=0)break;
                s->have=0;s->need=4;continue;
            }
        }
        if(at==(size_t)size)break;
        size_t n=s->need-s->have;if(n>(size_t)size-at)n=(size_t)size-at;
        memcpy(s->packet+s->have,bytes+at,n);s->have+=n;at+=n;
    }
    s->demuxing=0;return s->error?s->error:(int32_t)at;
}
int XbSonyMpeg_End(void *mp) {
    State *s=get(mp);if(!s || s->demuxing)return XB_MPEG_ARGUMENT;
    if(s->error)return s->error;
    if(s->have)return fail(s,XB_MPEG_FORMAT);
    s->ended=1;return XbMpeg_EndStream(s->decoder);
}
int32_t sceMpegIsEnd(void *mp) {State *s=get(mp);return !s || s->error || s->drained;}
int32_t sceMpegGetPicture(void *mp,uint8_t *out,int32_t blocks) {
    State *s=get(mp);if(!s || busy(s) || !out || blocks<896)return XB_MPEG_ARGUMENT;
    if(s->error)return s->error;
    if(s->drained)return -1;
    s->decoding=1;int result=XB_MPEG_NEED_INPUT;
    for(unsigned retry=0;retry<64;retry++) {
        result=XbMpeg_Next(s->decoder,s->rgba,XB_MPEG_RGBA_BYTES);
        if(result!=XB_MPEG_NEED_INPUT)break;
        if(!s->cb[1].call || !callback(s,s->cb[1],NULL))break;
        if(s->cb[4].call)(void)callback(s,s->cb[4],NULL);
        if(s->error){result=s->error;break;}
    }
    if(result==1) {
        for(unsigned x=0;x<32;x++)for(unsigned y=0;y<28;y++)for(unsigned row=0;row<16;row++) {
            uint8_t *d=out+((x*28+y)*256+row*16)*4;
            const uint8_t *a=s->rgba+((y*16+row)*512+x*16)*4;
            for(unsigned col=0;col<16;col++){memcpy(d+col*4,a+col*4,3);d[col*4+3]=0x80;}
        }
        s->object->width=512;s->object->height=448;
        s->object->pts=s->object->dts=(int64_t)s->object->frameCount*3003;
        s->object->frameCount++;
    } else if(!result)s->drained=1;
    else if(result<0)fail(s,result);
    s->decoding=0;return result==1?0:result==XB_MPEG_NEED_INPUT?XB_MPEG_NEED_INPUT:-1;
}
