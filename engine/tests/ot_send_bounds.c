/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef int s32;typedef uint32_t u32;typedef uint64_t u64;typedef uint8_t u8;
typedef struct OtPrim {uint32_t tag,pad;struct OtPrim *next;uint64_t vif[2];} OtPrim;
typedef struct {void *buf;unsigned bufSize;void *headBuf[2];} Ot;
#define OT_SLOT_COUNT 0x1002
#define OT_HEAD_SIZE 0x40
static Ot ot,*gOt=&ot;static OtPrim *gOtHead,*gOtTail;
static _Alignas(16) unsigned char packets[0x20000],draw[0x10010];
static int error,env,restore;static unsigned copied,chunks;
s32 Dma_GetFreeSize(void){return 0x100000;}
void xsdk_log_write(const char *f,...){(void)f;}
int dr_dma_producer_error(void){return error;}
int dr_dma_reject_packet(void){return error=-420;}
int dr_dma_w64_range(void*p,unsigned long size){assert((u8*)p>=draw && (u8*)p-draw<=0x10000);return size<=0x10000-(unsigned)((u8*)p-draw)?0:dr_dma_reject_packet();}
void Ot_AddEnv(int test){assert(!test);env++;}
void Ot_AddRestoreEnv(void){restore++;}
u64 *Dma_BeginDirect(void){return (u64*)draw;}
void Dma_EndDirect(u64*p){unsigned n=(unsigned)((u8*)p-draw);assert(n<=0x10000);copied+=n;chunks++;}
int dr_ot_payload(void*d,const void*s,unsigned long n,int b){(void)b;if(d)memcpy(d,s,n);return 0;}
/* ORIGINAL_FUNCTIONS */
static OtPrim *setup(unsigned qwc){
 memset(packets,0,sizeof(packets));memset(draw,0xCD,sizeof(draw));ot=(Ot){.buf=packets,.bufSize=sizeof(packets)};
 error=env=restore=0;copied=chunks=0;OtPrim*p=(void*)packets;p->tag=0x20000000|qwc;gOtHead=gOtTail=p;return p;
}
int main(void){
 OtPrim *p=setup(3);memset(p+1,0xA5,48);assert(Ot_Send()==1 && !error && copied==48 && env==1 && restore==1);for(unsigned i=0;i<48;i++)assert(draw[i]==0xA5);assert(draw[48]==0xCD);
 setup(0x1001);assert(!Ot_Send() && error && env==0 && copied==0 && draw[0]==0xCD);
 p=setup(0);p->next=p;assert(!Ot_Send() && error && !env);
 p=setup(1);p->next=(void*)(packets+sizeof(packets));assert(!Ot_Send() && error && !env);
 p=setup(1);p->next=(void*)(packets+1);assert(!Ot_Send() && error && !env);
 p=setup(0);gOtHead=(void*)(packets+sizeof(packets)-16);assert(!Ot_Send() && error && !env);
 setup(3);error=-999;assert(!Ot_Send() && error==-999 && copied==0);
 p=setup(2500);p->next=(void*)(packets+0x10000);p->next->tag=0x20000000|2500;p->next->next=NULL;
 assert(Ot_Send()==1 && copied==80000 && chunks==2 && env==1 && restore==1);
 setup(0);gOtTail=0;assert(!Ot_Send() && !env);
 puts("PASS adapted original Ot_Send: numeric low qwc, bounded payload/list/cycle/alignment; rejection before copies; sticky error and empty queue; ASan/UBSan host fixture");return 0;
}
