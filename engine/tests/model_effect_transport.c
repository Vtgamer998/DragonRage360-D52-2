/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_model_pack.h"
#include "xb_effect_pack.h"
#include "xb_resconv.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
void XbRes_TestRead(int,int,uint64_t,uint64_t,void*,size_t);
static unsigned le(const unsigned char*p){return XbRes_Le32(p);}
static unsigned word(const unsigned char*p){unsigned v;memcpy(&v,p,4);return v;}
static unsigned be(const unsigned char*p){return (unsigned)p[0]<<24|(unsigned)p[1]<<16|(unsigned)p[2]<<8|p[3];}
static int provenance(const void*p,const void*expected,size_t n,int expected_id){const void*b=(void*)1;size_t z=1;int id=1;int ok=XbRes_ReadFile(p,&b,&z,&id);if(expected)assert(ok&&b==expected&&z==n&&id==expected_id);else assert(!ok&&!b&&!z&&!id);return ok;}
int main(int argc,char**argv){
 assert(argc==5&&!XbModelPack_RegisterInitial()&&!XbEffectPack_RegisterInitial());
 const int ids[]={0x590,0x59a,0x599,0x5a3};
 for(unsigned i=0;i<4;i++){
  FILE*f=fopen(argv[i+1],"rb");assert(f&&!fseek(f,0,SEEK_END));long n=ftell(f);rewind(f);assert(n>1024);
  unsigned char*raw=malloc(n),*p=malloc(n),*before=malloc(n);assert(raw&&p&&before&&fread(raw,1,n,f)==(size_t)n);fclose(f);
  memcpy(p,raw,n);assert(!XbRes_BeginCheckedReads());assert(!provenance(p,NULL,0,0));XbRes_TestRead(1,ids[i]-1,0,n,p,n);assert(!XbRes_LastError());
  assert(provenance(p,p,n,ids[i])&&provenance(p+n-1,p,n,ids[i])&&!provenance(p+n,NULL,0,0));
  unsigned count=le(raw);for(unsigned j=0;j<count+2;j++)assert(word(p+j*4)==le(raw+j*4));
  if(i<2){
   unsigned a=word(p+12);const void*chain=p+a+word(p+a+0x6c);memcpy(before,p,n);
   for(unsigned repeat=0;repeat<2;repeat++)assert(XbModelPack_ChainPrepared(0,chain,0x3480,0x3c00,0x3d40,0x3d00,0x7fffffff,0x7fffffff)==1&&!memcmp(before,p,n));
   assert(XbModelPack_ChainPrepared(0,chain,1,0x3c00,0x3d40,0x3d00,0x7fffffff,0x7fffffff)==1&&XbRes_LastError());
   XbRes_Forget(p,n);assert(!provenance(chain,NULL,0,0)&&!XbModelPack_ChainPrepared(0,chain,0x3480,0x3c00,0x3d40,0x3d00,0x7fffffff,0x7fffffff));
  }
  XbRes_EndCheckedReads();assert(!provenance(p,NULL,0,0));
  unsigned offsets[]={0,4,4+count*4,le(raw+count*4)};
  for(unsigned j=0;j<4;j++){
   memcpy(p,raw,n);memset(p+offsets[j],0xff,4);memcpy(before,p,n);assert(!XbRes_BeginCheckedReads());XbRes_TestRead(1,ids[i]-1,0,n,p,n);assert(XbRes_LastError()&&!memcmp(p,before,n)&&!provenance(p,NULL,0,0));XbRes_EndCheckedReads();
  }
  memcpy(p,raw,n);memcpy(before,p,n);assert(!XbRes_BeginCheckedReads());XbRes_TestRead(1,ids[i]-1,1,n,p,n-1);assert(XbRes_LastError()&&!memcmp(p,before,n)&&!provenance(p,NULL,0,0));XbRes_EndCheckedReads();
  if(i<2){
   memcpy(p,raw,n);assert(!XbModelPack_Prepare(p,n,1));for(unsigned j=0;j<count+2;j++)assert(be(p+j*4)==le(raw+j*4));
   unsigned a=le(raw+8);assert(be(p+a)==le(raw+a)&&be(p+a+4)==le(raw+a+4));
   a=le(raw+12);assert(be(p+a+12)==le(raw+a+12)&&be(p+a+0x6c)==le(raw+a+0x6c));
   /* Canonical VIF commands are not byte swapped, although TEX0 addresses are rebased. */
   a=le(raw+4+3*4);assert(!memcmp(p+a,raw+a,16));
  }
  if(i>=2){
   XbCharacterEffectsInfo stats;
   memcpy(p,raw,n);assert(!XbEffectPack_Prepare(p,n,&stats));
   assert(stats.prepared_technique_packs==5&&!stats.unprepared_technique_packs);
   printf("PASS effects 0x%x: manager=%u sets=%u textures=%u sprites=%u cameras=%u techniques=%u unprepared=%u\n",ids[i],stats.manager_entries,stats.emitter_sets,stats.textures,stats.sprite_packs,stats.cameras,stats.prepared_technique_packs,stats.unprepared_technique_packs);
  }
  printf("PASS id=0x%x bytes=%ld entries=%u: strict whole-file conversion, provenance lifetime, atomic refusal; CPU only\n",ids[i],n,count);free(raw);free(p);free(before);
 }
 return 0;
}
