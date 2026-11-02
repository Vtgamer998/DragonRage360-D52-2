/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "dr_stage_collision.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
static uint32_t rd(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void wr(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static uint64_t hash=0xcbf29ce484222325ull;static unsigned seen;
static void word(uint32_t v){for(unsigned i=0;i<4;i++)hash=(hash^(uint8_t)(v>>(i*8)))*0x100000001b3ull;}
static int collect(void *ctx,uint32_t poly,const DrStageTriangle *t){(void)ctx;word(poly);word(t->flags);for(unsigned i=0;i<3;i++)for(unsigned j=0;j<4;j++){uint32_t bits;memcpy(&bits,&t->vertex[i][j],4);word(bits);}for(unsigned i=0;i<4;i++){uint32_t bits;memcpy(&bits,&t->plane[i],4);word(bits);}seen++;return 1;}
static void corrupt(uint8_t *pack,size_t n,size_t at,uint32_t value){
 uint32_t old=rd(pack+at);wr(pack+at,value);DrStageCollision v;memset(&v,0xa5,sizeof(v));assert(dr_stage_collision_open(pack,n,&v)<0);
 const uint8_t *q=(const uint8_t *)&v;for(size_t i=0;i<sizeof(v);i++)assert(!q[i]);assert(rd(pack+at)==value);wr(pack+at,old);
}
int main(int argc,char **argv){assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f && !fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0 && !fseek(f,0,SEEK_SET));size_t n=(size_t)size;uint8_t *p=malloc(n),*copy=malloc(n);assert(p && copy && fread(p,1,n,f)==n);fclose(f);memcpy(copy,p,n);
 DrStageCollision v;assert(!dr_stage_collision_open(p,n,&v));assert(v.zones==100 && v.meshes==151 && v.nodes==27015 && v.polygons==13583 && v.radius==1200 && v.top==-350 && v.bottom==1000);
 const float min[3]={-FLT_MAX,-FLT_MAX,-FLT_MAX},max[3]={FLT_MAX,FLT_MAX,FLT_MAX};
 for(uint32_t i=0;i<v.zones;i++){
  const uint8_t *z=v.base+v.zone_offset+(size_t)i*64;unsigned recs=rd(z+20);
  for(int r=-1;r<(int)recs;r++){int result;assert(!dr_stage_collision_query(&v,i,r,min,max,collect,NULL,&result));assert(result==0 || result==1);}
 }
 assert(seen==EXPECTED_TRIANGLES && hash==EXPECTED_HASH);
 DrStageTriangle t;assert(!dr_stage_collision_triangle(&v,0,-1,0,&t));assert(t.vertex[0][3]==1);
 assert(dr_stage_collision_triangle(&v,100,-1,0,&t)<0 && dr_stage_collision_triangle(&v,8,-1,0,&t)<0);
 size_t base=rd(p+4),zone=base+rd(p+base+60)*4,mesh=base+rd(p+zone+4)*4,nodes=base+rd(p+mesh+12)*4,poly=base+rd(p+mesh+16)*4;
 corrupt(p,n,base+56,UINT32_MAX);corrupt(p,n,mesh+12,UINT32_MAX);
 corrupt(p,n,nodes+24,0); /* inner root becomes a cycle */
 corrupt(p,n,base+rd(p+zone+12)*4,100);corrupt(p,n,poly+4,UINT32_MAX);corrupt(p,n,poly+16,0x7fc00000);
 DrStageCollision bad;assert(dr_stage_collision_open(p,base+64,&bad)<0);assert(!memcmp(copy,p,n));
 printf("PASS: %u triangle occurrences, independent fingerprint %016llx; 100 zones, static/destructible meshes, native floats, ordered closed AABB traversal; malformed pointers/counts/tree/vertex/NaN/truncation rejected; immutable input; host only\n",seen,(unsigned long long)hash);
 free(copy);free(p);return 0;
}
