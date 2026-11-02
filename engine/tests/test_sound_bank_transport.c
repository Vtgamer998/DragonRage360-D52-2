/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_sound_bank.h"
#include "xb_resconv.h"
#include "sys/snd.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
void XbRes_TestRead(int,int,uint64_t,uint64_t,void *,size_t);
static int XbSfx_TryVagMax(const void *p,int *out){(void)p;(void)out;return 0;}
static int XbSfx_TryLoopFlags(const void *p,void *out){(void)p;(void)out;return 0;}
static u32 SpuHeap_Alloc(s32 n){(void)n;assert(!"unexpected SPU allocation");return 0;}
static void *IopHeap_Alloc(s32 n){(void)n;assert(!"unexpected IOP allocation");return NULL;}
/* ORIGINAL_FUNCTIONS */
static unsigned be32(const uint8_t *p){return (unsigned)p[0]<<24|(unsigned)p[1]<<16|(unsigned)p[2]<<8|p[3];}
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
int main(int argc,char **argv){
 assert(argc==5 || argc==6 || argc==8);assert(!XbSoundBank_RegisterInitial());assert(!XbSoundBank_RegisterCommon());
 static const int ids[7]={0x14a,0x14e,0xbda,0xbdb,0x14b,0xc7b,0xc7c};static const unsigned masks[7]={4,8,16,32,1,16,32};
 for(unsigned i=0;i<(unsigned)argc-1;i++){
  FILE *f=fopen(argv[i+1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long length=ftell(f);assert(length>20);rewind(f);size_t n=(size_t)length;
  uint8_t *source=malloc(n),*native=malloc(n),*big=malloc(n),*bad=malloc(n),*before=malloc(n);assert(source && native && big && bad && before);
  assert(fread(source,1,n,f)==n);fclose(f);XbSoundBankInfo info,bi;memset(&info,0,sizeof(info));assert(!XbSoundBank_Inspect(source,n,masks[i],0,&info));
  memcpy(big,source,n);assert(!XbSoundBank_Prepare(big,n,masks[i],1));assert(be32(big)==3);assert(!memcmp(big+20,source+20,n-20));assert(!XbSoundBank_Inspect(big,n,masks[i],1,&bi));assert(!memcmp(&info,&bi,sizeof(info)));
  memcpy(native,source,n);assert(!XbRes_BeginCheckedReads());XbRes_TestRead(1,ids[i]-1,0,n,native,n);assert(!XbRes_LastError());assert(!XbSoundBank_RequireReadBank(native,masks[i]));
  int maximum=-1;uint8_t flags[129];memset(flags,0xa5,sizeof(flags));assert(XbSoundBank_TryVagMax(native,&maximum) && maximum==(int)info.max_sample);
  assert(XbSoundBank_TryLoopFlags(native,flags));assert(!memcmp(flags,info.loop,info.max_sample+1) && flags[info.max_sample+1]==0xa5);
  /* ORIGINAL Snd_SetupBank and metadata readers, with no device allocation. */
  SndBank bank;memset(&bank,0,sizeof(bank));Snd_SetupBank(&bank,masks[i],(SndBankFile *)native,0);
  assert(bank.body==(void *)(native+info.body) && bank.hd==(void *)(native+info.hd) && bank.seq==(void *)(native+info.sequence));
  assert(bank.bodySize==(s32)(info.hd-info.body) && bank.hdSize==(s32)(info.sequence-info.hd) && bank.seqSize==(s32)(info.end-info.sequence) && bank.vagMax==(s32)info.max_sample);
  for(unsigned j=0;j<info.max_sample;j++)assert(((bank.loopBits[j/8]>>(j%8))&1)==info.loop[j]);
  XbRes_Forget(native,n);assert(!XbSoundBank_TryVagMax(native,&maximum));assert(XbSoundBank_RequireReadBank(native,masks[i])<0);XbRes_EndCheckedReads();
  for(unsigned mode=0;mode<8;mode++){
   memcpy(bad,source,n);uint32_t hd=XbRes_Le32(source+8),end=XbRes_Le32(source+16);size_t vag=0;
   for(size_t at=hd;at+16<=n;at+=4)if(!memcmp(source+at,"IECSigaV",8)){vag=at;break;}
   assert(vag);
   if(mode==0)put(bad,4);
   if(mode==1)put(bad+4,16);
   if(mode==2)put(bad+8,UINT32_MAX);
   if(mode==3)put(bad+16,end+4);
   if(mode==4)put(bad+vag+12,128);
   if(mode==5)put(bad+vag+16,UINT32_MAX);
   if(mode==6){uint32_t at=XbRes_Le32(source+vag+16);bad[vag+at+6]=2;}
   if(mode==7){uint32_t at=XbRes_Le32(source+vag+16);bad[vag+at+4]=bad[vag+at+5]=0;}
   memcpy(before,bad,n);memset(&bi,0xa5,sizeof(bi));XbSoundBankInfo unchanged=bi;
   assert(XbSoundBank_Inspect(bad,n,masks[i],0,&bi)<0 && !memcmp(&bi,&unchanged,sizeof(bi)));
   assert(XbSoundBank_Prepare(bad,n,masks[i],1)<0 && !memcmp(bad,before,n));
  }
  printf("PASS real bank 0x%x: %zu bytes, max sample %u; read/conversion/original SetupBank; BE header, LE payload, metadata lifetime; 8 atomic refusals\n",ids[i],n,info.max_sample);
  free(before);free(bad);free(big);free(native);free(source);
 }
 return 0;
}
