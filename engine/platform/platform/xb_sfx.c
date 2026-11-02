/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Native single-owner sound sampler. ADPCM/interpolation/gain equations adapted
 * from port/src/gs/snd_se.c, upstream 3ca419c7966b11438e2dc3c9a7e1914f102ed545.
 * No SDL, IOP executable, sequencer binary or silent-success unknown commands.
 */
#include "xb_sfx.h"
#include "xb_iop.h"
#include "xb_adxt.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include "xsdk/log.h"
enum {SPU=0x200000,VOICES=48,BAD=-710,BOUNDS=-711,FORMAT=-712,UNSUPPORTED=-713,MEMORY=-714};
typedef struct {uint8_t *hd,*sq;size_t hn,sn;uint32_t spu,spu_end;int set,paused;float volume;} Bank;
typedef struct {int on,bank,id,handle,h1,h2,have,last,ended,stop;uint32_t pos,loop,limit;int16_t buf[28],prev;double frac,step;float left,right,fade;} Voice;
/* Owned by the game/main thread, including the synchronous vblank mixer.
 * RPC words are native endian; bank headers and ADPCM remain little endian. */
static uint8_t spu[SPU],written[SPU];static Bank banks[8];static Voice voices[VOICES];
static uint32_t upload_begin[64],upload_end[64],upload_next;
static void *clients[8];static int initialized,error,mono,transfer_done,mix_error;static int16_t fighters[11];static unsigned fighter_count;
int XbSfx_LastError(void){return error;}
int XbSfx_ActiveVoices(void){int n=0;for(unsigned i=0;i<VOICES;i++)n+=voices[i].on;return n;}
static int fail(int e){if(error!=e)xsdk_log_write("sfx: rejected operation error=%d",e);error=e;return e;}
static uint32_t le32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned le16(const uint8_t *p){return p[0]|(unsigned)p[1]<<8;}
static int32_t word(const void *p){int32_t v;memcpy(&v,p,4);return v;}
static unsigned half(const void *p){uint16_t v;memcpy(&v,p,2);return v;}
static const uint8_t *chunk(const uint8_t *p,size_t n,const char *tag,size_t minimum,size_t *size){
 for(size_t i=0;i+minimum<=n;i+=4)if(!memcmp(p+i,tag,8)){*size=n-i;return p+i;}
 return NULL;
}
#include "xb_sfx_banks.inc"
static int slot(int mask){if(mask<=0 || mask>255 || (mask&(mask-1)))return -1;int s=0;while((mask>>s)!=1)s++;return s;}
static int span(uint32_t at,size_t n){if(at>SPU || n>SPU-at)return 0;for(size_t i=0;i<n;i++)if(!written[at+i])return 0;return 1;}
static int lookup(const Bank *b,unsigned id,uint32_t *start,uint32_t *end,uint32_t *rate,int require_pcm){
 size_t vn=0,sn=0,tn=0;const uint8_t *v=chunk(b->hd,b->hn,"IECSigaV",16,&vn),*s=chunk(b->sq,b->sn,"IECSqseS",0x34,&sn),*t=chunk(b->hd,b->hn,"IECSbteS",0x24,&tn);
 if(!v || !s || !t || id>=256 || 0x34+(uint64_t)id*4+4>sn)return FORMAT;
 uint32_t o=le32(s+0x34+id*4);if(o>sn || sn-o<0x15 || s[o+0x11]!=0x90)return FORMAT;
 unsigned timbre=s[o+0x13],record=le16(t+0x20),max=le16(t+0x22);
 if(record<2 || timbre>max || 0x24+(uint64_t)timbre*record+2>tn)return FORMAT;
 unsigned sample=le16(t+0x24+timbre*record);uint32_t last=le32(v+12);
 if(last>255 || sample>last || 0x10+(uint64_t)(last+1)*4>vn)return FORMAT;
 uint32_t at=le32(v+0x10+sample*4);if(at>vn || vn-at<8)return FORMAT;
 uint32_t offset=le32(v+at),r=le16(v+at+4);if(!r || r>48000 || offset>SPU || b->spu>SPU-offset)return FORMAT;
 uint32_t a=b->spu+offset,limit=b->spu_end;
 for(unsigned i=0;i<=last;i++){uint32_t q=le32(v+0x10+i*4);if(q>vn || vn-q<8)return FORMAT;uint32_t next=le32(v+q);if(next>offset && next<=SPU-b->spu && b->spu+next<limit)limit=b->spu+next;}
 if(require_pcm && (a>=limit || limit-a<16 || !span(a,16)))return BOUNDS;
 *start=a;*end=limit;*rate=r;return 0;
}
static void stop_mask(int mask,int id,int handle){for(unsigned i=0;i<VOICES;i++){Voice *v=voices+i;if(v->on && ((handle>=0 && v->handle==handle)||(handle<0 && (mask&(1<<v->bank)) && (id<0 || id==v->id))))v->stop=1;}}
static int valid_play(const uint8_t *c){int s=slot(c[4]);uint32_t a,z,r;int pitch=word(c+8);if(s<0 || !banks[s].set || c[6]>127 || c[7]>127 || pitch<-1200 || pitch>1200)return BAD;return lookup(banks+s,c[5],&a,&z,&r,1);}
static void play(const uint8_t *c){int s=slot(c[4]);uint32_t a=0,z=0,r=0;if(lookup(banks+s,c[5],&a,&z,&r,1))return;unsigned pick=0;for(unsigned i=0;i<VOICES;i++)if(!voices[i].on){pick=i;break;}
 Voice *v=voices+pick;memset(v,0,sizeof(*v));v->on=1;v->bank=s;v->id=c[5];v->handle=(int)half(c+2);v->pos=a;v->limit=z;v->step=(double)r*pow(2.0,(double)word(c+8)/1200.0)/48000.;v->fade=1;
 double theta=((double)c[7]/127.)*1.5707963267948966;float gain=(float)c[6]/127.f*.25f/32768.f;
 v->left=(float)(cos(theta)*1.41421356)*gain;v->right=(float)(sin(theta)*1.41421356)*gain;
}
static int decode(Voice *v){static const int f0[5]={0,60,115,98,122},f1[5]={0,0,-52,-55,-60};
 if(v->last){v->ended=1;v->have=0;return 0;}
 if(v->pos>v->limit || v->limit-v->pos<16 || !span(v->pos,16))return fail(BOUNDS);
 const uint8_t *p=spu+v->pos;unsigned shift=p[0]&15,filter=p[0]>>4,flags=p[1];if(filter>4 || shift>12 || flags>7)return fail(FORMAT);
 if(flags&4)v->loop=v->pos;
 v->prev=v->have?v->buf[27]:0;
 for(unsigned i=0;i<28;i++){int nib=(p[2+i/2]>>((i&1)*4))&15;if(nib&8)nib-=16;int sample=(nib*4096>>shift)+((v->h1*f0[filter]+v->h2*f1[filter]+32)>>6);if(sample>32767)sample=32767;if(sample<-32768)sample=-32768;v->buf[i]=(int16_t)sample;v->h2=v->h1;v->h1=sample;}
 v->have=28;v->pos+=16;if(flags&1){if((flags&2) && v->loop)v->pos=v->loop;else v->last=1;}return 0;
}
int XbSfx_Mix(float *out){if(!out)return fail(BAD);if(!initialized)return 0;
 for(unsigned k=0;k<VOICES;k++){Voice *v=voices+k;if(!v->on || banks[v->bank].paused)continue;
  for(unsigned i=0;i<256;i++){while(v->frac>=v->have){v->frac-=v->have;if(decode(v)){v->on=0;mix_error=error;return error;}if(v->ended)break;}if(v->ended){v->on=0;break;}
   int ip=(int)v->frac;float a=(float)(ip?v->buf[ip-1]:v->prev),b=(float)v->buf[ip];float sample=(a+(b-a)*(float)(v->frac-ip))*v->fade*banks[v->bank].volume;
   if(mono){float g=(v->left+v->right)*.5f;out[i]+=sample*g;out[256+i]+=sample*g;}else{out[i]+=sample*v->left;out[256+i]+=sample*v->right;}v->frac+=v->step;if(v->stop){v->fade-=1.f/384.f;if(v->fade<=0){v->on=0;break;}}
  }
 }return 0;
}
void XbSfx_Close(void){for(unsigned i=0;i<8;i++){free(banks[i].hd);free(banks[i].sq);}memset(banks,0,sizeof(banks));memset(voices,0,sizeof(voices));memset(written,0,sizeof(written));initialized=mono=transfer_done=mix_error=0;upload_next=0;memset(upload_end,0,sizeof(upload_end));error=0;}
int XbSfx_Bind(void *p,int id,int mode){if(!p || id!=0x2000004 || mode)return fail(BAD);unsigned at=8;for(unsigned i=0;i<8;i++)if(clients[i]==p){at=i;break;}else if(!clients[i] && at==8)at=i;if(at==8)return fail(MEMORY);clients[at]=p;uint32_t mark=0x58534658;memcpy((uint8_t *)p+0x24,&mark,4);return 0;}
int func_002967C0(int block,int cmd,...){va_list args;va_start(args,cmd);int core=va_arg(args,int),mode=va_arg(args,int);int result=BAD;
 if(block==1 && core==1 && (mode==0 || mode==1)){
  if(cmd==0x80f0)result=transfer_done?1:fail(BAD);
  else if(cmd==0x80d0 && !mode){void *src=va_arg(args,void *);uint32_t at=va_arg(args,uint32_t),n=va_arg(args,uint32_t);size_t available=0;const void *view=XbIop_View((uintptr_t)src,&available);
   if(!view || !n || n>available || at>=SPU || n>SPU-at){transfer_done=0;result=fail(BOUNDS);}else{for(unsigned i=0;i<64;i++)if(upload_end[i]>at && upload_begin[i]<at+n)upload_end[i]=0;for(unsigned i=0;i<8;i++)if(banks[i].set && banks[i].spu_end>at && banks[i].spu<at+n){banks[i].spu_end=0;for(unsigned k=0;k<VOICES;k++)if(voices[k].bank==(int)i)voices[k].on=0;}memcpy(spu+at,view,n);memset(written+at,1,n);for(unsigned i=0;i<8;i++)if(banks[i].set && banks[i].spu==at)banks[i].spu_end=at+n;unsigned u=upload_next++%64;upload_begin[u]=at;upload_end[u]=at+n;transfer_done=1;result=0;}
  }else result=fail(UNSUPPORTED);
 }else result=fail(BAD);
 va_end(args);return result;
}
static int set_bank(const uint8_t *p){int s=slot(word(p));uint32_t at=(uint32_t)word(p+4);if(s<0 || at>=SPU)return BAD;size_t hn=0,sn=0;
 const void *hd=XbIop_View((uintptr_t)(uint32_t)word(p+8),&hn),*sq=XbIop_View((uintptr_t)(uint32_t)word(p+12),&sn);
 if(!hd || !sq || hn>131072 || sn>131072)return BOUNDS;
 uint32_t end=0;for(unsigned i=0;i<64;i++)if(upload_end[i] && at>=upload_begin[i] && at<upload_end[i] && upload_end[i]>end)end=upload_end[i];
 /* Metadata may arrive before the deferred sample transfer in Snd_Update. */
 Bank b={.hn=hn,.sn=sn,.spu=at,.spu_end=end,.set=1,.volume=1};b.hd=malloc(hn);b.sq=malloc(sn);if(!b.hd || !b.sq){free(b.hd);free(b.sq);return MEMORY;}memcpy(b.hd,hd,hn);memcpy(b.sq,sq,sn);
 uint32_t a,z,r;int e=lookup(&b,0,&a,&z,&r,0);if(e){free(b.hd);free(b.sq);return e;}
 for(unsigned i=0;i<VOICES;i++)if(voices[i].bank==s)voices[i].on=0;
 free(banks[s].hd);free(banks[s].sq);banks[s]=b;return 0;
}
int sceSifCallRpc(void *client,int fno,int mode,void *send,int size,void *recv,int recvSize,void *cb,void *arg){
 int found=0;for(unsigned i=0;i<8;i++)found|=clients[i]==client && client!=NULL;
 int e=0;if(!recv || recvSize<4 || recvSize>64)return fail(BAD);
 uint8_t input[388]; /* isolate send/receive aliasing before reply writes */
 if(!found || mode || cb || arg || !send || size<4 || size>388)e=BAD;
 else memcpy(input,send,(size_t)size);
 if(!e && fno!=0 && !initialized)e=BAD;
 if(!e && fno<=2 && size!=4)e=BAD;
 if(!e){const uint8_t *p=input;int mask=word(p),n;
  switch(fno){
  case 0: e=XbAdx_EnsureOutput();if(!e){XbSfx_Close();initialized=1;XbAdx_SetEffectsMixer(XbSfx_Mix,XbSfx_Close);}break;
  case 1:XbAdx_SetEffectsMixer(NULL,NULL);XbSfx_Close();for(unsigned i=0;i<8;i++)if(clients[i]==client)clients[i]=NULL;break;
  case 2:memset(voices,0,sizeof(voices));mix_error=0;break;
  case 3:e=size==16?set_bank(p):BAD;break;
  case 4:case 5:case 6:case 7:case 10:
   if((fno==6 || fno==7)?size!=8:size!=4){e=BAD;break;}
   if(fno==5){if(mask<0 || mask>1){e=BAD;break;}mono=mask;break;}
   if(mask<=0 || mask>255){e=BAD;break;}
   if(fno==7 && (word(p+4)<0 || word(p+4)>127)){e=BAD;break;}
   if(fno==6 && word(p+4)!=0 && word(p+4)!=1){e=BAD;break;}
   for(unsigned i=0;i<8;i++)if(mask&(1<<i)){
    if(fno==4){free(banks[i].hd);free(banks[i].sq);banks[i]=(Bank){0};for(unsigned k=0;k<VOICES;k++)if(voices[k].bank==(int)i)voices[k].on=0;}
    if(fno==6)banks[i].paused=word(p+4);
    if(fno==7)banks[i].volume=(float)word(p+4)/127.f;
   }if(fno==10)stop_mask(mask,-1,-1);break;
  case 9:e=size!=4?BAD:mix_error;break; /* service is the shared SDK audio hook */
  case 13:
   n=word(p);if(size!=388 || n<0 || n>32){e=BAD;break;}
   for(int i=0;i<n && !e;i++){const uint8_t *c=p+4+i*12;if(c[0]==0)e=valid_play(c);else if(c[0]>2)e=UNSUPPORTED;else if(c[0]==1 && c[4]==0)e=BAD;}
   if(!e)for(int i=0;i<n;i++){const uint8_t *c=p+4+i*12;if(c[0]==0)play(c);else stop_mask(c[4],c[0]==1?c[5]:-1,c[0]==2?(int)half(c+2):-1);}
   break;
  case 15:
   if(size!=24 || half(p)>11){e=BAD;break;}fighter_count=half(p);for(unsigned i=0;i<fighter_count;i++)fighters[i]=(int16_t)half(p+2+i*2);break;
  default:e=UNSUPPORTED;break;
  }
 }
 memset(recv,0,(size_t)recvSize);memcpy(recv,&e,4);if(e)return fail(e);error=0;return 0;
}
