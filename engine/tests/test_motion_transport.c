/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_motion_pack.h"
#include "xb_resconv.h"
#include "dr_container.h"
#include "battle/bobj_a.h"
#undef BOBJ_NODE_MAX
#include "battle/bobj_b.h"
#undef BOBJ_NODE_MAX
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
void XbRes_TestRead(int,int,uint64_t,uint64_t,void *,size_t);
void *Heap_Alloc(int32_t n,int32_t a,int32_t b,int32_t c){(void)a;(void)b;(void)c;return malloc((size_t)n);}
void Heap_Free(void *p){free(p);}
int XbFighter_LoadDecoded(void *a,void *b,int c,int *d,void **e){(void)a;(void)b;(void)c;(void)d;(void)e;return 0;}
/* ORIGINAL_FUNCTIONS */
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void put(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
int main(int argc,char **argv){
 assert(argc==3);assert(!XbMotionPack_RegisterInitial());
 for(unsigned side=0;side<2;side++){
  FILE *f=fopen(argv[side+1],"rb");assert(f);fseek(f,0,SEEK_END);long bytes=ftell(f);assert(bytes>0);size_t n=(size_t)bytes;rewind(f);
  uint8_t *source=malloc(n),*pack=malloc(n),*big=malloc(n),*out=malloc(0xc010),*expected=malloc(0xc000),*before=malloc(n);assert(source && pack && big && out && expected && before);
  assert(fread(source,1,n,f)==n);fclose(f);uint32_t count=XbRes_Le32(source),header=(count+2)*4;assert(count>=414 && count<=512);
  memcpy(big,source,n);assert(!XbMotionPack_Prepare(big,n,1));assert(be32(big)==count);
  for(unsigned i=0;i<=count;i++)assert(be32(big+4+i*4)==XbRes_Le32(source+4+i*4));
  assert(!memcmp(big+header,source+header,n-header));
  memcpy(pack,source,n);assert(!XbRes_BeginCheckedReads());XbRes_TestRead(1,(side?0x5a2:0x598)-1,0,n,pack,n);assert(!XbRes_LastError());
  BobjObj object;memset(&object,0,sizeof(object));BObjFile file;memset(&file,0,sizeof(file));file.buf=(u32 *)pack;
  unsigned live=0,empty=0;uint8_t *first=NULL;size_t first_span=0;
  for(unsigned i=0;i<414;i++){
   size_t at=0,len=0,written=0;int e=dr_pack_memory_entry(source,n,i,&at,&len,NULL);
   void *entry=BObjFile_GetEntry(&file,(int)i+1);object.mdl.anims[i]=entry;int32_t raw=77;memset(out,0xcd,0xc010);
   void *result=BtlObjAnim_Load(&object,out,(int)i,&raw);
   if(e==DR_NOT_FOUND){assert(!entry && !result && !raw);empty++;continue;}
   assert(!e && entry==pack+at && XbRes_BpeSourceSize(entry)==len);
   e=dr_motion_from_memory(source+at,len,1,0,expected,0xc000,&written);
   if(e==DR_LOAD_EMPTY){assert(!result && !raw && out[0]==0xcd);empty++;continue;}
   assert(!e && result==out && raw==(int32_t)written && !memcmp(out,expected,written));
   for(unsigned j=(unsigned)written;j<0xc010;j++)assert(out[j]==0xcd);
   if(!first){first=entry;first_span=len;}live++;
  }
  assert(first && first_span>=8 && !XbRes_LastError());
  int32_t raw=99;assert(!BtlObjAnim_Load(&object,NULL,0,&raw) && !raw);assert(!BtlObjAnim_Load(NULL,out,0,&raw) && !raw);assert(!BtlObjAnim_Load(&object,out,-1,&raw) && !raw);assert(!BtlObjAnim_Load(&object,out,422,&raw));
  XbRes_EndCheckedReads();
  /* Mutated inner size must not consume bytes from the next motion, even
   * though the entire file remains in bounds. No writes before refusal. */
  memcpy(pack,source,n);assert(!XbRes_BeginCheckedReads());XbRes_TestRead(1,(side?0x5a2:0x598)-1,0,n,pack,n);put(first+4,(uint32_t)first_span);memset(out,0xcd,0xc010);raw=99;
  assert(!XbBpe_DecodeBounded(first,out,0xc000,&raw) && !raw && XbRes_LastError());
  for(unsigned i=0;i<0xc010;i++)assert(out[i]==0xcd);
  XbRes_EndCheckedReads();
  memcpy(pack,source,n);assert(!XbRes_BeginCheckedReads());XbRes_TestRead(1,(side?0x5a2:0x598)-1,0,n,pack,n);memset(out,0xcd,0xc010);raw=99;
  assert(!XbBpe_DecodeBounded(first+4,out,0xc000,&raw) && !raw && XbRes_LastError());for(unsigned i=0;i<0xc010;i++)assert(out[i]==0xcd);
  XbRes_EndCheckedReads();
  for(unsigned mode=0;mode<4;mode++){
   memcpy(pack,source,n);if(mode==0)put(pack,513);if(mode==1)put(pack+4,0xfffffffc);if(mode==2)put(pack+8,0);
   if(mode==3){memcpy(pack,source,n);size_t at=(size_t)(first-pack);put(pack+at+4,UINT32_MAX);}
   memcpy(before,pack,n);assert(XbMotionPack_Prepare(pack,n,1) && !memcmp(pack,before,n));
  }
  memcpy(pack,source,n);assert(!XbRes_BeginCheckedReads());XbRes_TestRead(1,(side?0x5a2:0x598)-1,0,n,pack,n);XbRes_Forget(pack,n);assert(!XbRes_BpeSourceSize(first) && XbRes_LastError());XbRes_EndCheckedReads();
  printf("PASS motion file 0x%x entries=%u original slots: live=%u empty=%u; every inner format validated; original GetEntry/Anim_Load; exact spans, BE outer table, unchanged LE payload, refusal before output writes\n",side?0x5a2:0x598,count,live,empty);
  free(before);free(expected);free(out);free(big);free(pack);free(source);
 }
 return 0;
}
