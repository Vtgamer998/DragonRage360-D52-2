/* SPDX-License-Identifier: AGPL-3.0-or-later */
#define main iso_adapter_fixture_main
#include "test_xb_iso_file.c"
#undef main
#include "xb_iop.h"
#include "xb_adxt.h"
#include "xb_sfx.h"
#include "xsdk/audio.h"
#include <sys/mman.h>
#include <math.h>
static struct {void *p;size_t n;} allocations[64];
void *Port_LowAlloc(size_t n){n=(n+4095)&~(size_t)4095;void *p=mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);assert(p!=MAP_FAILED && (uintptr_t)p<=UINT32_MAX);for(unsigned i=0;i<64;i++)if(!allocations[i].p){allocations[i].p=p;allocations[i].n=n;return p;}assert(0);return NULL;}
void Port_LowFree(void *p){for(unsigned i=0;i<64;i++)if(allocations[i].p==p){assert(!munmap(p,allocations[i].n));allocations[i].p=NULL;return;}assert(0);}
int XbRes_LastError(void){return 0;}
void Port_VBlank(void){XbAdx_Tick();}
void XbRes_Forget(void *p,size_t n){(void)p;(void)n;}
static int device_open;static unsigned device_opens,device_closes;
NTSTATUS xsdk_audio_open(uint32_t n){assert(n==8 && !device_open);device_open=1;device_opens++;return STATUS_SUCCESS;}
NTSTATUS xsdk_audio_close(void){device_open=0;device_closes++;return STATUS_SUCCESS;}
NTSTATUS xsdk_audio_next_frame(uint32_t ms,float **p){(void)p;assert(!ms);return STATUS_IO_TIMEOUT;}
void xsdk_audio_queue_frame(void){assert(0);}
void xsdk_audio_stats(XsdkAudioStats *s){memset(s,0,sizeof(*s));}
int XbTime_OnVBlank(void (*fn)(void)){assert(fn);return 0;}
void Ref_Spu(const uint8_t *,size_t);void Ref_Start(uint32_t,uint32_t,int,int,int);void Ref_Mix(float *);
static uint32_t le(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint8_t client[40];static int32_t reply[16];
static int rpc(int cmd,void *p,int n){memset(reply,0x55,sizeof(reply));int e=sceSifCallRpc(client,cmd,0,p,n,reply,sizeof(reply),NULL,NULL);assert(reply[0]==e);return e;}
static uint32_t input[97];
static void reset(void){input[0]=0;assert(!rpc(2,input,4));}
static void play(int pitch,int pan){memset(input,0,sizeof(input));input[0]=1;uint8_t *c=(void *)(input+1);c[4]=1;c[6]=127;c[7]=(uint8_t)pan;uint16_t handle=19;memcpy(c+2,&handle,2);memcpy(c+8,&pitch,4);assert(!rpc(13,input,sizeof(input)));}
static float energy(const float *p){float n=0;for(unsigned i=0;i<512;i++)n+=fabsf(p[i]);return n;}
static uint8_t *disc_file(const char *path,size_t *n){dr_span s;assert(!XbDisc_FindFile(path,&s));*n=(size_t)s.size;uint8_t *p=malloc(*n);assert(p && !XbDisc_ReadSpan(&s,0,p,*n));return p;}
int main(int argc,char **argv){
 assert(argc==2);image_path=argv[1];assert(!XbDisc_Mount("game:/test.iso"));
 assert(!XbSfx_Bind(client,0x2000004,0));assert(XbSfx_Bind(client,0,0)<0);
 input[0]=0;assert(!rpc(0,input,4) && device_opens==1);
 /* Match Game_Main: Snd_Init opens first, File_Init then calls ADXT_Init. */
 ADXT_Init();assert(device_opens==1);
 size_t n;uint8_t *file=disc_file("pzs3us1/03194.bin",&n);assert(le(file)==3);
 uint32_t o[4];for(unsigned i=0;i<4;i++)o[i]=le(file+4+i*4);assert(o[3]<=n);
 void *body=sceSifAllocIopHeap((int)(o[1]-o[0])),*hd=sceSifAllocIopHeap((int)(o[2]-o[1])),*sq=sceSifAllocIopHeap((int)(o[3]-o[2]));assert(body && hd && sq);
 assert(!XbIop_Copy(body,file+o[0],(int)(o[1]-o[0])) && !XbIop_Copy(hd,file+o[1],(int)(o[2]-o[1])) && !XbIop_Copy(sq,file+o[2],(int)(o[3]-o[2])));
 assert(!func_002967C0(1,0x80d0,1,0,body,0x5010u,o[1]-o[0]));assert(func_002967C0(1,0x80f0,1,1)==1);
 input[0]=1;input[1]=0x5010;input[2]=(uint32_t)(uintptr_t)hd;input[3]=(uint32_t)(uintptr_t)sq;int e=rpc(3,input,16);if(e)fprintf(stderr,"SET_BANK error %d\n",e);assert(!e);
 const uint8_t *v=NULL;for(size_t i=0;i+16<o[2]-o[1];i+=4)if(!memcmp(file+o[1]+i,"IECSigaV",8)){v=file+o[1]+i;break;}assert(v);
 unsigned sample=0;const uint8_t *set=NULL,*seq=NULL;
 for(size_t i=0;i+36<o[2]-o[1];i+=4)if(!memcmp(file+o[1]+i,"IECSbteS",8)){set=file+o[1]+i;break;}
 for(size_t i=0;i+56<o[3]-o[2];i+=4)if(!memcmp(file+o[2]+i,"IECSqseS",8)){seq=file+o[2]+i;break;}
 assert(set && seq);unsigned timbre=seq[le(seq+0x34)+0x13],stride=set[0x20]|set[0x21]<<8;const uint8_t *s=set+0x24+timbre*stride;sample=s[0]|s[1]<<8;
 const uint8_t *record=v+le(v+0x10+sample*4);uint32_t start=le(record),rate=record[4]|record[5]<<8;
 Ref_Spu(file+o[0],o[1]-o[0]);float a[1536],b[1536],c[1536];
 for(int pitch=-1200;pitch<=1200;pitch+=1200){reset();play(pitch,64);Ref_Start(start,rate,pitch,127,64);for(unsigned f=0;f<8;f++){memset(a,0,sizeof(a));assert(!XbAdx_Mix(a));Ref_Mix(b);for(unsigned i=0;i<512;i++)assert(fabsf(a[i]-b[i])<1.f/32768.f);}}
 reset();play(0,64);input[0]=1;input[1]=1;assert(!rpc(6,input,8));assert(!XbAdx_Mix(a) && energy(a)==0);input[1]=0;assert(!rpc(6,input,8));assert(!XbAdx_Mix(a) && energy(a)>0);
 input[0]=1;input[1]=0;assert(!rpc(7,input,8));assert(!XbAdx_Mix(a) && !energy(a));input[1]=127;assert(!rpc(7,input,8));assert(!XbAdx_Mix(a) && energy(a)>0);
 reset();play(0,0);assert(!XbAdx_Mix(a));for(unsigned i=0;i<256;i++)assert(a[256+i]==0);
 input[0]=1;assert(!rpc(5,input,4));assert(!XbAdx_Mix(a));for(unsigned i=0;i<256;i++)assert(a[i]==a[256+i]);input[0]=0;assert(!rpc(5,input,4));
 reset();play(0,64);input[0]=1;assert(!rpc(10,input,4));assert(!XbAdx_Mix(a) && !XbAdx_Mix(a) && !XbSfx_ActiveVoices());
 reset();memset(input,0,sizeof(input));input[0]=2;uint8_t *cmd=(void *)(input+1);cmd[4]=1;cmd[6]=127;cmd[7]=64;cmd[12]=99;assert(rpc(13,input,sizeof(input))<0 && !XbSfx_ActiveVoices());
 input[0]=33;assert(rpc(13,input,sizeof(input))<0);input[0]=0;assert(rpc(8,input,4)<0 && rpc(0x77,input,4)<0);
 /* Combined ADXT + effects equals the clipped sum, through the one mixer. */
 size_t adx_n;uint8_t *adx=disc_file("pzs3us2/00000.bin",&adx_n);uint8_t work[128];void *h=ADXT_Create(2,work,sizeof(work));assert(h);
 reset();assert(!XbAdx_StartBytes(h,adx,adx_n) && !XbAdx_Mix(a));
 assert(!XbAdx_StartBytes(h,adx,adx_n));play(0,64);assert(!XbAdx_Mix(b));ADXT_Stop(h);reset();play(0,64);assert(!XbAdx_Mix(c));
 for(unsigned i=0;i<512;i++){float v0=a[i]+c[i];if(v0>1)v0=1;if(v0<-1)v0=-1;assert(fabsf(v0-b[i])<1e-6f);}assert(device_opens==1);
 /* Deferred Snd_LoadBank: metadata precedes upload. PLAY refuses until ready. */
 input[0]=2;input[1]=0x100000;input[2]=(uint32_t)(uintptr_t)hd;input[3]=(uint32_t)(uintptr_t)sq;assert(!rpc(3,input,16));
 reset();memset(input,0,sizeof(input));input[0]=1;cmd=(void *)(input+1);cmd[4]=2;cmd[6]=127;cmd[7]=64;assert(rpc(13,input,sizeof(input))<0);
 assert(!func_002967C0(1,0x80d0,1,0,body,0x100000u,o[1]-o[0]));assert(!rpc(13,input,sizeof(input)));assert(!XbAdx_Mix(a) && energy(a)>0);
 /* A replacement transfer stops old voices, then permits replay of the bank. */
 assert(!func_002967C0(1,0x80d0,1,0,body,0x100000u,o[1]-o[0]) && !XbSfx_ActiveVoices());assert(!rpc(13,input,sizeof(input)));
 /* Banks own their copies after IOP buffers are freed. */
 assert(!sceSifFreeIopHeap(hd) && !sceSifFreeIopHeap(sq));reset();play(0,64);assert(!XbAdx_Mix(a) && energy(a)>0);
 assert(func_002967C0(1,0x80d0,1,0,body,0x1ffff8u,16u)<0);
 assert(func_002967C0(1,0x80d0,1,0,(void *)(uintptr_t)1,0x5010u,16u)<0);
 /* Bad blocks fail in the mixer and remain observable through TICK. */
 ((uint8_t *)body)[start]=0xf0;assert(!func_002967C0(1,0x80d0,1,0,body,0x100000u,o[1]-o[0]));
 memset(input,0,sizeof(input));input[0]=1;cmd=(void *)(input+1);cmd[4]=2;cmd[6]=127;cmd[7]=64;assert(!rpc(13,input,sizeof(input)));assert(XbAdx_Mix(a)<0);input[0]=0;assert(rpc(9,input,4)<0);
 reset();assert(!rpc(9,input,4));
 assert(!sceSifFreeIopHeap(body));input[0]=0;assert(!rpc(1,input,4) && !XbSfx_ActiveVoices());assert(!XbAdx_Close() && device_closes==1);
 free(file);free(adx);XbDisc_Unmount();puts("PASS: real ISO bank, owned IOP->SPU transfer, native RPC/queue, 6144 reference samples at three pitches, pan/mono/pause/volume/stop, atomic queue refusal, bank lifetime, deferred/replacement uploads, malformed ADPCM error propagation and real ADXT+SFX sum; device/NT mocked");return 0;
}
