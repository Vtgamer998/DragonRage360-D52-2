/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_battle_stage.h"
#include "battle/col_a.h"
#include "battle/eft_det_b.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
/* Real storage/one vector-copy primitive for the original host modules.
 * These are not success stubs; no other discarded engine path is called. */
StgColStage *gBtlStage;void *gStgColMesh;s32 gStgColMode;
s32 *gColMeshBase; s32 gColWalkCount,gColWalkMax;u16 *gColWalkBuf;
ColBox *gColWalkBox;ColMeshCb gColWalkCb;void *gColWalkCtx;
void Vec4_Copy(ColVec *d,ColVec *s){memmove(d,s,sizeof(*d));}
static uint64_t hash;static unsigned seen;static XbBattleStageCollision *context;
static void word(uint32_t v){for(unsigned i=0;i<4;i++)hash=(hash^(uint8_t)(v>>(i*8)))*0x100000001b3ull;}
static int collect(void *ctx,uint32_t poly,const DrStageTriangle *t){
 (void)ctx;word(poly);word(t->flags);for(unsigned i=0;i<3;i++)for(unsigned j=0;j<4;j++){uint32_t bits;memcpy(&bits,&t->vertex[i][j],4);word(bits);}for(unsigned i=0;i<4;i++){uint32_t bits;memcpy(&bits,&t->plane[i],4);word(bits);}seen++;
 assert(XbBattleStage_UninstallCollision(context)==XB_STAGE_STATE);return 1;
}
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f && !fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0 && !fseek(f,0,SEEK_SET));size_t n=(size_t)size;uint8_t *p=malloc(n);assert(p && fread(p,1,n,f)==n);fclose(f);
 assert(!XbBattleStage_PrepareCollision(p,n,&context) && context && !gBtlStage);
 free(p);p=NULL; /* arena must own every live pointer */
 assert(!XbBattleStage_InstallCollision(context));assert(gBtlStage && gBtlStage->objs && gColMeshBase);
 assert(XbBattleStage_InstallCollision(context)==XB_STAGE_STATE && XbBattleStage_FreeCollision(context)==XB_STAGE_STATE);
 const float min[3]={-FLT_MAX,-FLT_MAX,-FLT_MAX},max[3]={FLT_MAX,FLT_MAX,FLT_MAX};
 for(unsigned broken=0;broken<2;broken++){
  for(uint32_t i=0;i<gBtlStage->objCount;i++)gBtlStage->objs[i].state=broken;
  hash=0xcbf29ce484222325ull;seen=0;
  for(uint32_t i=0;i<100;i++){int result;assert(!XbBattleStage_QueryCollision(context,i,min,max,collect,NULL,&result));}
  assert(seen==(broken?EXPECTED_BROKEN_N:EXPECTED_WHOLE_N));assert(hash==(broken?EXPECTED_BROKEN_HASH:EXPECTED_WHOLE_HASH));
  printf("PASS native original collision: broken=%u triangles=%u fingerprint=%016llx\n",broken,seen,(unsigned long long)hash);
 }
 assert(!XbBattleStage_UninstallCollision(context) && !gBtlStage && !gColMeshBase);
 int result=77;assert(XbBattleStage_QueryCollision(context,0,min,max,collect,NULL,&result)==XB_STAGE_STATE && !result);
 assert(!XbBattleStage_FreeCollision(context));return 0;
}
