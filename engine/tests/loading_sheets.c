/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "sys/loading.h"
#include "xb_resconv.h"
#include "xb_demo_resources.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
LoadScreen gLoadScreen;
static Progress progress;
Progress *gProgress=&progress;
static uint8_t packed[0x3000],expanded[0x6800];
static LoadSprite sprites[LOAD_SPRITE_COUNT];
static int screen,relocations,corrupt;
static const char **paths;
void XbRes_TestRead(int,int,uint64_t,uint64_t,void *,size_t);
void *Heap_Alloc(int32_t n,int32_t a,int32_t b,int32_t c){(void)a;(void)b;(void)c;return malloc((size_t)n);}
void Heap_Free(void *p){free(p);}
static int Rand_Range(int n){assert(n>0);return screen<n?screen:0;}
static int Rand_IntRange(int a,int b){assert(b>=a);return a;}
/* Simulated IO delivers the real source bytes through the real read hook. */
static void *File_LoadSync(int id,void *dst,int capacity){
 assert(id==0x3c1+screen && dst==packed && capacity==sizeof(packed));
 FILE *f=fopen(paths[screen],"rb");assert(f);size_t n=fread(dst,1,(size_t)capacity,f);assert(n>8 && feof(f));fclose(f);
 if(corrupt)memset(dst,0xff,4);
 XbRes_TestRead(1,id-1,0,n,dst,n);return dst;
}
/* 64-bit host cannot use the target's 32-bit pointer slots. Check the
 * pre-fixup contract only; original relocation is not executed here. */
static void Res_RelocateOffsets(void **out,void *base,void *hdr){
 assert(base==expanded && hdr==expanded);assert(!XbRes_LastError());
 assert(XbRes_Le32(expanded)>=(screen==1?3u:2u));
 assert(!XbRes_Le32(expanded+0x10));*out=hdr;relocations++;
}
/* ORIGINAL_FUNCTIONS */
int main(int argc,char **argv){
 assert(argc==4);paths=(const char **)&argv[1];assert(!XbRes_RegisterLoadingScreens());
 for(int round=0;round<2;round++)for(screen=0;screen<3;screen++){
  memset(&progress,0,sizeof(progress));progress.loadPack=packed;progress.loadRes=expanded;progress.loadSprites=sprites;
  memset(sprites,0xa5,sizeof(sprites));relocations=0;assert(!XbRes_BeginCheckedReads());Load_InitScreen();
  assert(!XbRes_LastError() && relocations==1 && gLoadScreen.type==screen && gLoadScreen.sprites==sprites && gLoadScreen.res==expanded);
  unsigned textures=XbRes_Le32(expanded);for(unsigned i=0;i<LOAD_SPRITE_COUNT;i++)if(sprites[i].flags==LOAD_SPR_DRAW)assert(sprites[i].tex>=0 && (unsigned)sprites[i].tex<textures);
  assert(progress.lastLoadType==screen+1 && sprites[1].flags==LOAD_SPR_END);
  XbRes_EndCheckedReads();
 }
 for(screen=0;screen<3;screen++){
  memset(&progress,0,sizeof(progress));progress.loadPack=packed;progress.loadRes=expanded;progress.loadSprites=sprites;
  corrupt=1;relocations=0;memset(sprites,0xa5,sizeof(sprites));assert(!XbRes_BeginCheckedReads());Load_InitScreen();
  assert(XbRes_LastError() && !relocations && !gLoadScreen.res && !gLoadScreen.sprites);
  for(unsigned i=0;i<sizeof(sprites);i++)assert(((uint8_t *)sprites)[i]==0xa5);
  XbRes_EndCheckedReads();
 }
 puts("PASS original Load_InitScreen + Load_BuildSprites with 3 real sheets, twice; strict read/BPE/conversion; texture references bounded; corrupt BPE never reaches relocation or sprite construction. IO/fixup mocked, no GPU.");return 0;
}
