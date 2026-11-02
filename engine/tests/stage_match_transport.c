/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_stage_pack.h"
#include "xb_match_ui_pack.h"
#include "xb_resconv.h"
#include "dr_texfile.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
void XbRes_TestRead(int,int,uint64_t,uint64_t,void*,size_t);
static unsigned rd(const void*p){return XbRes_Le32(p);}
static unsigned native(const void*p){unsigned v;memcpy(&v,p,4);return v;}
int main(int argc,char**argv){
 assert(argc==3&&!XbStagePack_RegisterInitial()&&!XbMatchUiPack_RegisterInitial());
 const int ids[]={0x171,6};
 for(unsigned i=0;i<2;i++){
  FILE*f=fopen(argv[i+1],"rb");assert(f&&!fseek(f,0,SEEK_END));long n=ftell(f);rewind(f);assert(n>1024);
  uint8_t*raw=malloc(n),*p=malloc(n),*before=malloc(n);assert(raw&&p&&before&&fread(raw,1,n,f)==(size_t)n);fclose(f);
  memcpy(p,raw,n);assert(!XbRes_BeginCheckedReads());XbRes_TestRead(1,ids[i]-1,0,n,p,n);assert(!XbRes_LastError());
  unsigned count=rd(raw);for(unsigned j=0;j<count+2;j++)assert(native(p+j*4)==rd(raw+j*4));
  if(!i){
   XbStagePackInfo stats;memcpy(before,raw,n);assert(!XbStagePack_Prepare(before,n,&stats)&&stats.entries==15);
   assert(stats.objects==11&&stats.groups==7&&!stats.timers);
   void*base=p+native(p+4),*empty=XbStagePack_NullMesh(base);assert(empty!=base);for(unsigned j=0;j<8;j++)assert(!((uint32_t*)empty)[j]);
   XbRes_Forget(p,n);assert(XbStagePack_NullMesh(base)==base);
   printf("PASS stage0: entries=%u groups=%u materials=%u meshes=%u parts=%u nodes=%u cells=%u objects=%u timers=%u textures=%u\n",stats.entries,stats.groups,stats.materials,stats.meshes,stats.parts,stats.nodes,stats.cells,stats.objects,stats.timers,stats.textures);
  }
  if(i){
   unsigned at=rd(raw+4+6*4),size=rd(raw+4+7*4)-at;uint8_t rgba[4096],old[4096];DrTexFileImage image;
   assert(!dr_texfile_validate_transfers(raw+at,size,3));
   memset(rgba,0xa5,sizeof(rgba));memcpy(old,rgba,sizeof(old));
   assert(dr_texfile_decode(raw+at,size,3,rgba,sizeof(rgba),&image)&&!memcmp(rgba,old,sizeof(old)));
  }
  XbRes_EndCheckedReads();
  unsigned offsets[10]={0,4,4+count*4,rd(raw+4)};unsigned checks=4;
  if(!i){offsets[3]+=24;unsigned first=rd(raw+4),map=rd(raw+8);offsets[checks++]=first+28;offsets[checks++]=first+80;offsets[checks++]=first+84;offsets[checks++]=map+12;offsets[checks++]=rd(raw+4+5*4);offsets[checks++]=rd(raw+4+15*4)+4;}
  else{offsets[checks++]=rd(raw+4+7*4);offsets[checks++]=rd(raw+4)+4;}
  for(unsigned j=0;j<checks;j++){
   memcpy(p,raw,n);memset(p+offsets[j],0xff,4);memcpy(before,p,n);assert(!XbRes_BeginCheckedReads());XbRes_TestRead(1,ids[i]-1,0,n,p,n);assert(XbRes_LastError()&&!memcmp(p,before,n));XbRes_EndCheckedReads();
  }
  memcpy(p,raw,n);assert(!XbRes_BeginCheckedReads());XbRes_TestRead(1,ids[i]-1,0,n,p,n-1);assert(XbRes_LastError()&&!memcmp(p,raw,n));XbRes_EndCheckedReads();
  printf("PASS id=0x%x: %ld bytes, %u entries; %u atomic refusals and partial read refused; CPU only\n",ids[i],n,count,checks);
  free(raw);free(p);free(before);
 }
}
