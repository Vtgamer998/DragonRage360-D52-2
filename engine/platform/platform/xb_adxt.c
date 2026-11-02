/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Native ADXT subset for the USA disc's type-3, version-4, 18-byte ADPCM.
 * Decode equations/coefficients match the public pinned port's snd_adx.c.
 * No CRI/SDL binary or PS2 code is linked. Whole files are prefetched from ISO;
 * playback/mixing and ISO access share the main thread's vblank hook.
 */
#include "xb_adxt.h"
#include "xb_file.h"
#include "xb_time.h"
#include "xsdk/audio.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
enum { PLAYERS=16,MAX_FILE=32*1024*1024,MAX_TOTAL=64*1024*1024,STOP=0,PLAY=3,END=5 };
enum { BAD=-650,FORMAT=-651,IO=-652,MEMORY=-653,DEVICE=-654 };
typedef struct {
    int used,max_channels,status,paused,volume,pan[2],reload,freq,reading;
    uint8_t *file;size_t size,data;uint32_t channels,rate,total,pos,loop_start,loop_end;
    int loop,loop_have,rendering,c1,c2,hist[2][2],loop_hist[2][2];
    int16_t cache[64];unsigned cache_n,cache_at,phase;int hold;
    int16_t left,right;int64_t ticks;float gain;
    uint64_t mixed48,played48;uint32_t generation;
} Player;
typedef struct {uint32_t serial,generation[PLAYERS];uint64_t end[PLAYERS];} AudioStamp;
static AudioStamp stamps[32];static uint32_t submitted,collected,generation;
static Player players[PLAYERS];static int opened,hooked,error,mono,device_dead;
static int (*effects_mix)(float *);static void (*effects_close)(void);
void XbAdx_SetEffectsMixer(int (*mix)(float *),void (*close_fn)(void)){effects_mix=mix;effects_close=close_fn;}
static int reject(int e){error=e;return e;}
int XbAdx_LastError(void){return error;}
static Player *player(void *h) {
    uintptr_t a=(uintptr_t)h,b=(uintptr_t)players;
    if(a<b || a-b>=sizeof(players) || (a-b)%sizeof(Player) || !players[(a-b)/sizeof(Player)].used){reject(BAD);return NULL;}
    return &players[(a-b)/sizeof(Player)];
}
static uint32_t be16(const uint8_t *p){return (uint32_t)p[0]<<8|p[1];}
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void stop(Player *p){p->generation=++generation;p->mixed48=p->played48=0;free(p->file);p->file=NULL;p->size=0;p->status=STOP;p->rendering=0;p->ticks=0;p->hold=0;p->cache_n=p->cache_at=0;}
int XbAdx_Close(void) {
    void (*close_fn)(void)=effects_close;effects_mix=NULL;effects_close=NULL;if(close_fn)close_fn();
    for(unsigned i=0;i<PLAYERS;i++){stop(players+i);players[i].used=0;}
    device_dead=1;
    if(opened && !NT_SUCCESS(xsdk_audio_close()))return reject(DEVICE);
    opened=0;mono=0;error=0;device_dead=0;submitted=collected=0;memset(stamps,0,sizeof(stamps));return 0;
}
void ADXT_Init(void) {
    /* Snd_Init precedes File_Init: both share the already-open SDK client. */
    if(opened && !device_dead){error=0;return;}
    if(XbAdx_Close())return;
    if(!hooked){if(XbTime_OnVBlank(XbAdx_Tick)){reject(DEVICE);return;}hooked=1;}
    if(!NT_SUCCESS(xsdk_audio_open(8))){reject(DEVICE);return;}
    opened=1;
}
int XbAdx_EnsureOutput(void){if(!opened || device_dead)ADXT_Init();return opened && !device_dead?0:DEVICE;}
void *ADXT_Create(int channels,void *work,int worksize) {
    if(!opened || device_dead || channels<1 || channels>2 || !work || worksize<128){reject(BAD);return NULL;}
    for(unsigned i=0;i<PLAYERS;i++)if(!players[i].used){
        Player *p=players+i;memset(p,0,sizeof(*p));p->used=1;p->max_channels=channels;
        p->pan[0]=p->pan[1]=-128;p->reload=16;p->freq=30;p->gain=1.0f/32768.0f;return p;
    }
    reject(MEMORY);return NULL;
}
int XbAdx_Destroy(void *h){Player *p=player(h);if(!p)return BAD;stop(p);p->used=0;return 0;}
void ADXT_SetReloadSct(void *h,int n){Player *p=player(h);if(p && n>0 && n<=512)p->reload=n;else reject(BAD);}
void ADXT_SetSvrFreq(void *h,int n){Player *p=player(h);if(p && n>0 && n<=60)p->freq=n;else reject(BAD);}
void ADXT_Stop(void *h){Player *p=player(h);if(p)stop(p);}
void ADXT_Pause(void *h,int on){Player *p=player(h);if(p)p->paused=!!on;}
void ADXT_SetOutVol(void *h,int vol){Player *p=player(h);if(p && vol>=-960 && vol<=0){p->volume=vol;p->gain=vol<=-960?0.0f:powf(10.0f,vol/200.0f)/32768.0f;}else reject(BAD);}
int ADXT_GetOutVol(void *h){Player *p=player(h);return p?p->volume:0;}
void ADXT_SetOutPan(void *h,int ch,int pan){Player *p=player(h);if(p && ch>=0 && ch<2 && (pan==-128 || (pan>=-16 && pan<=15)))p->pan[ch]=pan;else reject(BAD);}
void ADXT_SetOutputMono(int on){mono=!!on;}
int ADXT_GetStat(void *h){Player *p=player(h);return p?p->status:STOP;}
/* Native synchronous prefetch state, not an IOP driver status guess. */
int func_00277A70(void *h){Player *p=player(h);return p?p->reading:0;}
static int adopt(Player *p,uint8_t *file,size_t size) {
    stop(p);error=0;
    if(size<0x40 || size>MAX_FILE || be16(file)!=0x8000 || file[4]!=3 || file[5]!=18 || file[6]!=4 ||
       file[7]<1 || file[7]>(unsigned)p->max_channels || file[0x12]!=4 || (file[0x13]&8)){free(file);return reject(FORMAT);}
    size_t start=be16(file+2)+4;uint32_t rate=be32(file+8),total=be32(file+12),hp=be16(file+16);
    uint64_t need=((uint64_t)total+31)/32*18*file[7];
    if((rate!=16000 && rate!=24000 && rate!=48000) || !total || hp>=rate/2 || start<0x20 || start>size || need>size-start || memcmp(file+start-6,"(c)CRI",6)){free(file);return reject(FORMAT);}
    p->loop=start>=0x38 && be32(file+0x24)!=0;p->loop_start=be32(file+0x28);p->loop_end=be32(file+0x30);
    if(p->loop && (p->loop_start%32 || p->loop_start>=p->loop_end || p->loop_end>total)){free(file);return reject(FORMAT);}
    double z=cos(2.0*3.14159265358979323846*hp/rate),a=1.41421356237309504880-z,b=1.41421356237309504880-1.0;
    double c=(a-sqrt((a+b)*(a-b)))/b;
    p->c1=(int)(c*8192.0);p->c2=(int)(c*c*-4096.0);
    p->file=file;p->size=size;p->data=start;p->channels=file[7];p->rate=rate;p->total=total;
    p->pos=0;p->cache_n=p->cache_at=p->phase=0;p->hold=p->loop_have=0;memset(p->hist,0,sizeof(p->hist));
    p->ticks=p->loop?-1:(int64_t)(((uint64_t)total*60000+(uint64_t)rate*1001-1)/((uint64_t)rate*1001));
    p->status=PLAY;p->rendering=1;return 0;
}
static int within_budget(Player *replace,size_t size) {
    size_t used=size;
    for(unsigned i=0;i<PLAYERS;i++)if(players+i!=replace && players[i].file){used+=players[i].size;if(used>MAX_TOTAL)return 0;}
    return used<=MAX_TOTAL;
}
int XbAdx_StartBytes(void *h,const void *bytes,size_t size) {
    Player *p=player(h);if(!p)return BAD;
    if(!bytes || size>MAX_FILE){stop(p);return reject(FORMAT);}
    if(!within_budget(p,size)){stop(p);return reject(MEMORY);}
    uint8_t *copy=malloc(size?size:1);if(!copy){stop(p);return reject(MEMORY);}memcpy(copy,bytes,size);return adopt(p,copy,size);
}
static void start_file(Player *p,int part,int id,const char *name) {
    dr_span span;stop(p);p->reading=1;
    if(XbFile_Locate(part,id,name,&span) || !span.size || span.size>MAX_FILE){p->reading=0;reject(IO);return;}
    if(!within_budget(p,(size_t)span.size)){p->reading=0;reject(MEMORY);return;}
    uint8_t *bytes=malloc((size_t)span.size);if(!bytes){p->reading=0;reject(MEMORY);return;}
    size_t at=0,chunk=(size_t)p->reload*2048;
    while(at<span.size){size_t n=(size_t)span.size-at;if(n>chunk)n=chunk;if(XbFile_Read(&span,at,bytes+at,n)!=n){free(bytes);p->reading=0;reject(IO);return;}at+=n;}
    p->reading=0;adopt(p,bytes,(size_t)span.size);
}
void ADXT_StartAfs(void *h,int part,int id){Player *p=player(h);if(p)start_file(p,part,id,NULL);}
void ADXT_StartFname(void *h,char *name){Player *p=player(h);if(p){if(!name){stop(p);reject(BAD);}else start_file(p,0,0,name);}}
static int next_sample(Player *p,int16_t *left,int16_t *right) {
    uint32_t end=p->loop?p->loop_end:p->total;
    if(p->pos>=end){if(!p->loop)return 0;p->pos=p->loop_start;memcpy(p->hist,p->loop_hist,sizeof(p->hist));p->cache_n=p->cache_at=0;}
    if(p->cache_at==p->cache_n){
        if(p->loop && p->pos==p->loop_start && !p->loop_have){memcpy(p->loop_hist,p->hist,sizeof(p->hist));p->loop_have=1;}
        const uint8_t *block=p->file+p->data+(size_t)(p->pos/32)*18*p->channels;
        for(unsigned ch=0;ch<p->channels;ch++){
            const uint8_t *src=block+ch*18;int h1=p->hist[ch][0],h2=p->hist[ch][1],scale=(int)be16(src)+1;
            for(unsigned i=0;i<32;i++){int nib=(src[2+i/2]>>((i&1)?0:4))&15;if(nib&8)nib-=16;
                int64_t acc=(int64_t)p->c1*h1+(int64_t)p->c2*h2;int sample=nib*scale+(int)(acc>>12);
                if(sample>32767)sample=32767;
                if(sample<-32768)sample=-32768;
                p->cache[i*2+ch]=(int16_t)sample;h2=h1;h1=sample;}
            p->hist[ch][0]=h1;p->hist[ch][1]=h2;
        }
        p->cache_at=0;p->cache_n=end-p->pos<32?end-p->pos:32;
    }
    *left=p->cache[p->cache_at*2];*right=p->channels==2?p->cache[p->cache_at*2+1]:0;
    p->cache_at++;p->pos++;return 1;
}
int XbAdx_Mix(float *frame) {
    if(!opened || device_dead || !frame)return reject(BAD);
    memset(frame,0,sizeof(float)*XSDK_AUDIO_FRAME_FLOATS);
    for(unsigned k=0;k<PLAYERS;k++){
        Player *p=players+k;if(!p->used || !p->rendering || p->paused || !p->file)continue;
        float gain=p->gain;
        float gl[2],gr[2];
        for(unsigned ch=0;ch<2;ch++){int pan=mono?0:p->pan[ch]==-128?(p->channels==1?0:ch==0?-15:15):p->pan[ch];
            if(pan<-15)pan=-15;
            gl[ch]=pan<=0?1.0f:(15-pan)/15.0f;gr[ch]=pan>=0?1.0f:(15+pan)/15.0f;}
        for(unsigned i=0;i<256;i++){
            if(!p->hold){if(!next_sample(p,&p->left,&p->right)){p->rendering=0;break;}p->hold=1;}
            frame[i]+=gain*(p->left*gl[0]+p->right*gl[1]);frame[256+i]+=gain*(p->left*gr[0]+p->right*gr[1]);
            p->mixed48++;p->phase+=p->rate;if(p->phase>=48000){p->phase-=48000;p->hold=0;}
        }
    }
    if(effects_mix){int e=effects_mix(frame);if(e)return reject(e);}
    for(unsigned i=0;i<512;i++){if(frame[i]>1)frame[i]=1;if(frame[i]<-1)frame[i]=-1;}
    return 0;
}
static int collect_audio(void) {
    XsdkAudioStats stats;xsdk_audio_stats(&stats);
    uint32_t available=stats.played-collected;
    if(available>32 || stats.played-collected>submitted-collected)return reject(DEVICE);
    while(collected!=stats.played) {
        uint32_t serial=++collected;AudioStamp *stamp=&stamps[serial%32];
        if(stamp->serial!=serial)return reject(DEVICE);
        for(unsigned i=0;i<PLAYERS;i++)if(players[i].used && stamp->generation[i]==players[i].generation)
            players[i].played48=stamp->end[i];
    }
    return 0;
}
int XbAdx_Clock(void *h,uint64_t *ns,int *ended) {
    Player *p=player(h);if(!p || !ns || !ended || !opened || !p->file)return BAD;
    if(collect_audio())return DEVICE;
    *ns=p->played48*1000000000ull/48000;
    *ended=!p->loop && p->played48>=(uint64_t)p->total*48000/p->rate;
    return 0;
}
void XbAdx_Tick(void) {
    if(!opened || device_dead)return;
    if(collect_audio())return;
    unsigned quota=effects_mix?8:1;
    for(unsigned i=0;i<PLAYERS;i++)if(players[i].used && (players[i].status==PLAY || players[i].rendering)){
        Player *p=players+i;unsigned need=(188u+(unsigned)p->freq-1)/(unsigned)p->freq+1;if(need>8)need=8;if(need>quota)quota=need;
        if(!p->paused && p->ticks>0 && --p->ticks==0)p->status=END;
    }
    for(unsigned i=0;i<quota;i++){
        float *frame=NULL;NTSTATUS s=xsdk_audio_next_frame(0,&frame);
        if(s==STATUS_IO_TIMEOUT)break;
        if(!NT_SUCCESS(s) || !frame){reject(DEVICE);break;}
        if(XbAdx_Mix(frame))break;
        if(submitted-collected>=32){reject(DEVICE);break;}
        AudioStamp *stamp=&stamps[(submitted+1)%32];stamp->serial=++submitted;
        for(unsigned k=0;k<PLAYERS;k++){stamp->generation[k]=players[k].generation;stamp->end[k]=players[k].mixed48;}
        xsdk_audio_queue_frame();
    }
}
