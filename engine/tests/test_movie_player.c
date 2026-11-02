/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Actual adapted player/ADXF/ADXT/decoder/ISO. NT, GPU, pad, fade, game heap
 * and device pacing are fixtures; this does not execute Xbox hardware. */
#define main iso_adapter_fixture_main
#include "test_xb_iso_file.c"
#undef main
#include "port_compat.h"
#undef malloc
#include "sys/movie.h"
#include "sys/gfx.h"
#include "sys/pad.h"
#include "xb_adxt.h"
#include "xb_time.h"
#include "xsdk/audio.h"
#include "dr_gs_movie.h"
Movie *gMovie;AdxPlayer gAdxPlayerTbl[6];Gfx gGfx;Pad gPad[2];
s32 gMovieRunCount,gMovieEsSize;u8 *gMovieEsBuf,*gMovieEsWrite,*gMovieEsDmaStart,*gMovieEsDmaEnd;
char gMovieTagIdle[]="idle",gMovieTagNoData[]="nodata",gMovieTagFull[]="full";
static uint64_t now,device_next;static unsigned queued,played,silent;
static unsigned presented,skipped,frames,heap_live;static int paused,fade,opened;
static float audio_frame[1536];static void (*hook)(void);
static void advance_audio(void) {
    while(device_next<=now) {
        if(played<queued)played++;else silent++;
        device_next+=256ull*1000000000/48000;
    }
}
uint64_t XbTime_NowNs(void){return now;}
int XbTime_OnVBlank(void (*fn)(void)){assert(!hook);hook=fn;return 0;}
void Port_VBlank(void){now+=16683350;advance_audio();if(hook)hook();}
NTSTATUS xsdk_audio_open(uint32_t n){assert(n==8);queued=played=silent=0;device_next=now+5333333;opened=1;return STATUS_SUCCESS;}
NTSTATUS xsdk_audio_close(void){opened=0;return STATUS_SUCCESS;}
NTSTATUS xsdk_audio_next_frame(uint32_t wait,float **out){assert(!wait && opened);advance_audio();if(queued-played>=8)return STATUS_IO_TIMEOUT;*out=audio_frame;return STATUS_SUCCESS;}
void xsdk_audio_queue_frame(void){assert(queued-played<8);queued++;}
void xsdk_audio_stats(XsdkAudioStats *s){advance_audio();memset(s,0,sizeof(*s));s->played=played;s->silent=silent;}
void *Port_LowAlloc(size_t size){return calloc(1,size);}
void Port_LowFree(void *p){free(p);}
void *Heap_Alloc(s32 bytes,s32 alignment,s32 flags,s32 heap){(void)alignment;(void)flags;(void)heap;assert(bytes>0);void *p=calloc(1,(size_t)bytes);assert(p);heap_live++;return p;}
void Heap_Free(void *p){if(p){assert(heap_live);heap_live--;free(p);}}
void Adx_PlayFilePaused(s32 ch,char *name,s32 vol,s32 pan){assert(ch==0 && vol==0x40 && !pan);ADXT_Pause(gAdxPlayerTbl[0].adxt,1);ADXT_StartFname(gAdxPlayerTbl[0].adxt,name);assert(!XbAdx_LastError());paused=1;}
void Adx_Stop(s32 ch){assert(!ch);ADXT_Stop(gAdxPlayerTbl[0].adxt);}
void Adx_Resume(s32 ch){assert(!ch && paused);ADXT_Pause(gAdxPlayerTbl[0].adxt,0);paused=0;}
void Gfx_BeginFrame(void){frames++;if(frames==12)now+=100000000;}
void Gfx_EndFrame(s32 fields){assert(fields==1);Port_VBlank();gGfx.field^=1;gGfx.frame++;presented++;}
void Pad_Update(void){memset(gPad,0,sizeof(gPad));if(frames==32){gPad[0].gamePressed=MOVIE_SKIP_BUTTON;skipped++;}}
void Fade_Start(s32 slot,s32 type,f32 duration){(void)type;(void)duration;assert(!slot);fade=1;}
s32 Fade_IsDone(s32 slot){assert(!slot);return fade;}
void Fade_Reset(s32 slot){assert(!slot);fade=0;}
void Dma_Flush(void){}
s32 sceGsSyncPath(s32 mode,u16 timeout){assert(!mode && !timeout);return 0;}
/* This boundary is tested with the actual GS/core/backend in test_capture.
 * Here verify the player keeps a valid live decoded buffer until upload. */
int dr_gs_movie_build(void *tags,const uint8_t *image,int step,int x,int y,int w,int h){assert(step==1024 && !x && (y==0 || y==448) && w==512 && h==448);memcpy(tags,&image,sizeof(image));return 0;}
int dr_gs_movie_send(const void *tags){const uint8_t *image;memcpy(&image,tags,sizeof(image));assert(image && image[3]==0x80);return 0;}
int main(int argc,char **argv) {
    assert(argc==2);image_path=argv[1];assert(!XbDisc_Mount("game:/test.iso"));
    ADXT_Init();uint8_t work[128];gAdxPlayerTbl[0].adxt=ADXT_Create(2,work,sizeof(work));assert(gAdxPlayerTbl[0].adxt);
    Movie_Init();Movie_PlayOpening();assert(!gMovie && !heap_live && skipped==1 && presented>=32 && gMovieRunCount==1);
    unsigned before=presented;Movie_Open("absent.pss","zs3usop.adx");Movie_Run();Movie_Close();assert(!gMovie && !heap_live && presented==before);
    assert(!XbAdx_Close());XbDisc_Unmount();
    printf("PASS: adapted original Movie_PlayOpening/Run/Close, real ISO PSS and ADX, actual streaming codec/SDK-consumed audio clock, START/cleanup, missing-file refusal; %u mock presents, synthetic 100ms stall, NT/GPU/pacing/pad/heap mocked\n",presented);return 0;
}
