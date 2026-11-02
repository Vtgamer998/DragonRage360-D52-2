/* SPDX-License-Identifier: MIT; native audio code, mocked xsdk transport. */
#include "xb_adxt.h"
#include "xb_file.h"
#include "xb_time.h"
#include "xsdk/audio.h"
#include "reference.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static int available,queued,read_calls,played,auto_play=1;static float device_frame[XSDK_AUDIO_FRAME_FLOATS];
static NTSTATUS open_result=STATUS_SUCCESS,next_result=STATUS_SUCCESS,close_result=STATUS_SUCCESS;
static void (*tick_hook)(void);
NTSTATUS xsdk_audio_open(uint32_t n){assert(n==8);queued=played=0;return open_result;}
NTSTATUS xsdk_audio_close(void){return close_result;}
NTSTATUS xsdk_audio_next_frame(uint32_t timeout,float **p){assert(!timeout);if(!NT_SUCCESS(next_result))return next_result;if(!available)return STATUS_IO_TIMEOUT;*p=device_frame;return STATUS_SUCCESS;}
void xsdk_audio_stats(XsdkAudioStats *s){memset(s,0,sizeof(*s));s->played=(uint32_t)(auto_play?queued:played);}
void xsdk_audio_queue_frame(void){assert(available>0);available--;queued++;}
int XbTime_OnVBlank(void (*fn)(void)){assert(!tick_hook);tick_hook=fn;return 0;}
static uint8_t *fixture;static size_t fixture_size;
int XbFile_Locate(int part,int id,const char *name,dr_span *s){assert((part==2 && id==0 && !name) || (part==0 && id==0 && name && !strcmp(name,"TEST.ADX")));*s=(dr_span){0,fixture_size,0};return 0;}
size_t XbFile_Read(const dr_span *s,uint64_t off,void *data,size_t n){assert(s->size==fixture_size && off<=s->size && n<=s->size-off);read_calls++;memcpy(data,fixture+off,n);return n;}
static unsigned be16(const uint8_t *p){return p[0]*256u+p[1];}
static unsigned be32(const uint8_t *p){return p[0]*16777216u+p[1]*65536u+p[2]*256u+p[3];}
static void word(uint8_t *p,uint32_t n){p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n;}
static unsigned work[128];
static void *create(void){void *p=ADXT_Create(2,work,sizeof(work));assert(p);return p;}
static void compare(uint8_t *data,size_t size){
    void *h=create();assert(!XbAdx_StartBytes(h,data,size));
    RefPlayer ref={0};ref.file=data;ref.size=(uint32_t)size;ref.data=be16(data+2)+4;
    ref.channels=data[7];ref.rate=be32(data+8);ref.total=be32(data+12);ref.pan[0]=ref.pan[1]=-128;
    Ref_coefs(be16(data+16),ref.rate,&ref.c1,&ref.c2);
    int16_t pcm[32768];int samples=Ref_decode(&ref,pcm,512);assert(samples>256);
    float frame[XSDK_AUDIO_FRAME_FLOATS];unsigned ratio=48000/ref.rate;
    unsigned frames=(unsigned)samples*ratio/256;if(frames>192)frames=192;
    unsigned nonzero=0;
    for(unsigned block=0;block<frames;block++) {
        assert(!XbAdx_Mix(frame));
        for(unsigned i=0;i<256;i++){
            unsigned at=((block*256+i)/ratio)*2;
            assert(fabsf(frame[i]-pcm[at]/32768.0f)<0.000001f);
            assert(fabsf(frame[256+i]-pcm[at+1]/32768.0f)<0.000001f);
            nonzero+=pcm[at]!=0 || pcm[at+1]!=0;
        }
        for(unsigned i=512;i<XSDK_AUDIO_FRAME_FLOATS;i++)assert(frame[i]==0);
    }
    assert(nonzero>0);
    ADXT_Stop(h);assert(ADXT_GetStat(h)==0);
}
int main(int argc,char **argv){
    assert(argc>=2);ADXT_Init();assert(!XbAdx_LastError() && tick_hook);
    size_t n=0x40+1024/32*18;uint8_t *data=calloc(1,n);assert(data);
    data[0]=0x80;data[3]=0x3c;data[4]=3;data[5]=18;data[6]=4;data[7]=1;data[0x12]=4;
    word(data+8,48000);word(data+12,1024);data[16]=1;data[17]=0xf4;memcpy(data+0x40-6,"(c)CRI",6);
    for(size_t i=0x40;i<n;i+=18){data[i+1]=4;memset(data+i+2,0x12,16);}
    compare(data,n);
    for(int arg=1;arg<argc;arg++){
        FILE *f=fopen(argv[arg],"rb");assert(f);assert(!fseek(f,0,SEEK_END));size_t size=(size_t)ftell(f);rewind(f);
        uint8_t *bytes=malloc(size);assert(bytes && fread(bytes,1,size,f)==size);fclose(f);
        compare(bytes,size);free(bytes);
    }
    assert(!XbAdx_Close());ADXT_Init();void *h=create();fixture=data;fixture_size=n;
    ADXT_SetReloadSct(h,1);ADXT_SetSvrFreq(h,30);ADXT_StartAfs(h,2,0);assert(read_calls>0 && ADXT_GetStat(h)==3);
    ADXT_Pause(h,1);float frame[XSDK_AUDIO_FRAME_FLOATS];assert(!XbAdx_Mix(frame));for(unsigned i=0;i<XSDK_AUDIO_FRAME_FLOATS;i++)assert(!frame[i]);
    for(unsigned i=0;i<100;i++)tick_hook();
    assert(ADXT_GetStat(h)==3);
    ADXT_Pause(h,0);ADXT_SetOutVol(h,-600);assert(ADXT_GetOutVol(h)==-600);
    ADXT_StartFname(h,"TEST.ADX");available=2;queued=0;tick_hook();assert(queued==2 && ADXT_GetStat(h)==3);
    available=0;tick_hook();assert(ADXT_GetStat(h)==5); /* logical clock independent of queue availability */
    assert(!XbAdx_StartBytes(h,data,n));ADXT_SetOutVol(h,-960);assert(!XbAdx_Mix(frame));for(unsigned i=0;i<512;i++)assert(!frame[i]);
    ADXT_SetOutVol(h,0);ADXT_SetOutPan(h,0,15);assert(!XbAdx_StartBytes(h,data,n));assert(!XbAdx_Mix(frame));
    for(unsigned i=0;i<256;i++)assert(frame[i]==0);
    assert(frame[256]>0);
    ADXT_SetOutPan(h,0,-128);ADXT_SetOutputMono(1);assert(!XbAdx_StartBytes(h,data,n));assert(!XbAdx_Mix(frame));for(unsigned i=0;i<256;i++)assert(frame[i]==frame[256+i]);ADXT_SetOutputMono(0);
    word(data+0x24,1);word(data+0x28,0);word(data+0x30,64);assert(!XbAdx_StartBytes(h,data,n));assert(!XbAdx_Mix(frame));
    for(unsigned i=64;i<256;i++)assert(frame[i]==frame[i%64]);
    word(data+0x30,63);assert(!XbAdx_StartBytes(h,data,n));assert(!XbAdx_Mix(frame));
    for(unsigned i=63;i<256;i++)assert(frame[i]==frame[i%63]);
    ADXT_Pause(h,0);for(unsigned i=0;i<100;i++)tick_hook();
    assert(ADXT_GetStat(h)==3);
    uint8_t *bad=malloc(n);assert(bad);
    for(unsigned kind=0;kind<8;kind++){
        memcpy(bad,data,n);
        if(kind==0)bad[0]=0;
        if(kind==1)bad[4]=4;
        if(kind==2)word(bad+8,0);
        if(kind==3)bad[7]=3;
        if(kind==4)bad[0x13]=8;
        if(kind==5)word(bad+12,0xffffffff);
        if(kind==6)bad[0x40-1]=0;
        if(kind==7)word(bad+0x28,1);
        assert(XbAdx_StartBytes(h,bad,n)!=0 && ADXT_GetStat(h)==0);
    }
    ADXT_Pause((void *)(uintptr_t)1,1);assert(XbAdx_LastError()!=0);
    ADXT_SetSvrFreq(h,0);assert(XbAdx_LastError()!=0);ADXT_SetReloadSct(h,-1);assert(XbAdx_LastError()!=0);
    next_result=STATUS_INVALID_PARAMETER;tick_hook();assert(XbAdx_LastError()!=0);next_result=STATUS_SUCCESS;
    assert(!XbAdx_Close());ADXT_Init();
    size_t large_size=22u*1024*1024;uint8_t *large=calloc(1,large_size);assert(large);memcpy(large,data,64);
    word(large+12,(uint32_t)((large_size-64)/18*32));
    void *big1=create(),*big2=create(),*big3=create();assert(!XbAdx_StartBytes(big1,large,large_size) && !XbAdx_StartBytes(big2,large,large_size));
    assert(XbAdx_StartBytes(big3,large,large_size)!=0 && ADXT_GetStat(big1)==3 && ADXT_GetStat(big2)==3 && ADXT_GetStat(big3)==0);free(large);
    close_result=STATUS_INVALID_PARAMETER;assert(XbAdx_Close()!=0 && !ADXT_Create(2,work,sizeof(work)));close_result=STATUS_SUCCESS;
    assert(!XbAdx_Close());ADXT_Init();for(unsigned i=0;i<16;i++)assert(ADXT_Create(2,work,sizeof(work)));assert(!ADXT_Create(2,work,sizeof(work)));
    assert(!XbAdx_Close());open_result=STATUS_INVALID_PARAMETER;ADXT_Init();assert(!ADXT_Create(2,work,sizeof(work)));
    open_result=STATUS_SUCCESS;ADXT_Init();assert(create());assert(!XbAdx_Close());
    /* SDK-consumed sample clock: queued-ahead samples must not move it. */
    ADXT_Init();h=create();assert(!XbAdx_StartBytes(h,data,n));
    auto_play=0;available=8;tick_hook();
    uint64_t ns=99;int ended=99;
    assert(!XbAdx_Clock(h,&ns,&ended) && ns==0 && !ended && queued>0);
    played=1;assert(!XbAdx_Clock(h,&ns,&ended) && ns==256ull*1000000000/48000);
    ADXT_Pause(h,1);played=queued;assert(!XbAdx_Clock(h,&ns,&ended));uint64_t before=ns;
    available=2;tick_hook();played=queued;assert(!XbAdx_Clock(h,&ns,&ended) && ns==before);
    ADXT_Stop(h);assert(XbAdx_Clock(h,&ns,&ended)!=0);
    assert(!XbAdx_StartBytes(h,data,n));assert(!XbAdx_Clock(h,&ns,&ended) && ns==0);
    assert(!XbAdx_Close());auto_play=1;
    free(data);free(bad);
    puts("PASS: native ADXT, real 16/24k and synthetic 48k ADX PCM matches original decoder sample-hold resampling; controls, loop predictor restore, deterministic tick status, bounded ISO read path and 6-channel xsdk queue mocks; invalid formats/device errors rejected; no hardware audio");
}
