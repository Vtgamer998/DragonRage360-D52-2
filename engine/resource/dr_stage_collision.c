/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "dr_stage_collision.h"
#include "dr_container.h"
#include <math.h>
#include <string.h>
#include <limits.h>
static uint32_t rd(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static float fl(const uint8_t *p){uint32_t u=rd(p);float f;memcpy(&f,&u,4);return f;}
static int span(const DrStageCollision *v,uint32_t word,size_t n,size_t *at){
 if((uint64_t)word*4>v->size)return DR_STAGE_BOUNDS;
 size_t o=(size_t)word*4;if(o>v->size || n>v->size-o)return DR_STAGE_BOUNDS;*at=o;return 0;
}
static int array(const DrStageCollision *v,uint32_t word,uint32_t n,size_t stride,size_t *at){
 if(n>v->size/stride)return DR_STAGE_BOUNDS;
 return span(v,word,(size_t)n*stride,at);
}
static int floats(const uint8_t *p,unsigned n){for(unsigned i=0;i<n;i++)if(!isfinite(fl(p+4*i)))return DR_STAGE_NUMBER;return 0;}
static int triangle(const DrStageCollision *v,size_t m,uint32_t poly,DrStageTriangle *out){
 const uint8_t *h=v->base+m;size_t p;if(poly>v->size/32 || span(v,rd(h+16),(size_t)(poly+1)*32,&p))return DR_STAGE_BOUNDS;
 p+=(size_t)poly*32;const uint8_t *q=v->base+p;
 if(floats(q+16,4))return DR_STAGE_NUMBER;
 DrStageTriangle t;t.flags=rd(q);for(unsigned i=0;i<4;i++)t.plane[i]=fl(q+16+4*i);
 for(unsigned i=0;i<3;i++){
  uint32_t index=rd(q+4+4*i);size_t a;
  if(index>v->size/16 || span(v,rd(h+20),(size_t)(index+1)*16,&a))return DR_STAGE_BOUNDS;
  a+=(size_t)index*16;if(floats(v->base+a,4))return DR_STAGE_NUMBER;
  for(unsigned j=0;j<4;j++)t.vertex[i][j]=fl(v->base+a+4*j);
 }
 if(out)*out=t;
 return 0;
}
static int walk(const DrStageCollision *v,size_t mesh,size_t nodes,uint32_t count,uint32_t index,unsigned depth,uint32_t *budget){
 if(index>=count || depth>=64 || !*budget)return DR_STAGE_TREE;
 --*budget;const uint8_t *p=v->base+nodes+(size_t)index*32;
 if(floats(p,6))return DR_STAGE_NUMBER;
 for(unsigned i=0;i<3;i++)if(fl(p+4*i)>fl(p+12+4*i))return DR_STAGE_NUMBER;
 uint32_t left=rd(p+24),right=rd(p+28);
 if(right==UINT32_MAX){if(left==UINT32_MAX)return 0;return triangle(v,mesh,left,NULL);}
 int e=walk(v,mesh,nodes,count,left,depth+1,budget);return e?e:walk(v,mesh,nodes,count,right,depth+1,budget);
}
static int mesh(DrStageCollision *v,uint32_t word){
 /* Null static meshes occur in seven stage-0 boundary zones. Keep absent;
  * never interpret the stage header itself as a collision mesh. */
 if(!word)return 0;
 size_t m;if(span(v,word,24,&m))return DR_STAGE_BOUNDS;
 uint32_t n=rd(v->base+m);if((int32_t)n<=0)return 0;
 size_t nodes;if(array(v,rd(v->base+m+12),n,32,&nodes))return DR_STAGE_BOUNDS;
 uint32_t budget=n>UINT32_MAX/2?UINT32_MAX:n*2;
 int e=walk(v,m,nodes,n,0,0,&budget);if(e)return e;
 /* Count stored leaves for diagnostics; all reachable leaves were checked. */
 uint32_t polygons=0;for(uint32_t i=0;i<n;i++)if(rd(v->base+nodes+(size_t)i*32+28)==UINT32_MAX && rd(v->base+nodes+(size_t)i*32+24)!=UINT32_MAX)polygons++;
 if(v->nodes>UINT32_MAX-n || v->polygons>UINT32_MAX-polygons || v->meshes==UINT32_MAX)return DR_STAGE_BOUNDS;
 v->nodes+=n;v->polygons+=polygons;v->meshes++;return 0;
}
int dr_stage_collision_open(const void *pack,size_t bytes,DrStageCollision *out){
 if(!out)return DR_STAGE_FORMAT;
 memset(out,0,sizeof(*out));size_t off,n;if(!pack || bytes>UINT32_MAX || dr_pack_memory_entry(pack,bytes,0,&off,&n,NULL) || n<96)return DR_STAGE_FORMAT;
 DrStageCollision v={0};v.base=(const uint8_t *)pack+off;v.size=n;
 v.zones=rd(v.base+56);v.objects=rd(v.base+68);size_t z,b;
 if(!v.zones || array(&v,rd(v.base+60),v.zones,64,&z) || span(&v,rd(v.base+64),12,&b))return DR_STAGE_BOUNDS;
 if(z>UINT32_MAX)return DR_STAGE_BOUNDS;
 v.zone_offset=(uint32_t)z;
 if(floats(v.base+b,3))return DR_STAGE_NUMBER;
 v.radius=fl(v.base+b);v.top=fl(v.base+b+4);v.bottom=fl(v.base+b+8);
 if(v.radius<=0 || v.top>v.bottom)return DR_STAGE_NUMBER;
 for(uint32_t i=0;i<v.zones;i++){
  const uint8_t *q=v.base+z+(size_t)i*64;size_t near,recs;uint32_t nn=rd(q+8),nr=rd(q+20);
  if(array(&v,rd(q+12),nn,4,&near) || array(&v,rd(q+24),nr,16,&recs))return DR_STAGE_BOUNDS;
  if(floats(q+32,8))return DR_STAGE_NUMBER;
  for(unsigned j=0;j<3;j++)if(fl(q+48+4*j)<0)return DR_STAGE_NUMBER;
  for(uint32_t j=0;j<nn;j++)if(rd(v.base+near+(size_t)j*4)>=v.zones)return DR_STAGE_BOUNDS;
  int e=mesh(&v,rd(q+4));if(e)return e;
  for(uint32_t j=0;j<nr;j++){
   const uint8_t *r=v.base+recs+(size_t)j*16;
   if(rd(r+4)>=v.objects)return DR_STAGE_BOUNDS;
   e=mesh(&v,rd(r+8));if(e)return e;
  }
 }
 *out=v;return 0;
}
int dr_stage_collision_triangle(const DrStageCollision *v,uint32_t zone,int record,uint32_t poly,DrStageTriangle *out){
 if(!out)return DR_STAGE_FORMAT;
 memset(out,0,sizeof(*out));
 if(!v || !v->base || zone>=v->zones || record< -1)return DR_STAGE_BOUNDS;
 size_t z=v->zone_offset+(size_t)zone*64;if(z>v->size || v->size-z<64)return DR_STAGE_BOUNDS;
 const uint8_t *q=v->base+z;uint32_t word=rd(q+4);
 if(record>=0){size_t r;if((uint32_t)record>=rd(q+20) || array(v,rd(q+24),rd(q+20),16,&r))return DR_STAGE_BOUNDS;word=rd(v->base+r+(size_t)record*16+8);}
 if(!word)return DR_STAGE_BOUNDS;
 size_t m;if(span(v,word,24,&m))return DR_STAGE_BOUNDS;
 /* Public requests must identify a polygon actually present in this tree. */
 uint32_t count=rd(v->base+m);size_t nodes;if(array(v,rd(v->base+m+12),count,32,&nodes))return DR_STAGE_BOUNDS;
 int found=0;for(uint32_t i=0;i<count;i++){const uint8_t *p=v->base+nodes+(size_t)i*32;if(rd(p+28)==UINT32_MAX && rd(p+24)==poly){found=1;break;}}
 if(!found || poly==UINT32_MAX)return DR_STAGE_BOUNDS;
 return triangle(v,m,poly,out);
}

static int query_node(const DrStageCollision *v,size_t m,size_t nodes,uint32_t count,uint32_t index,
 unsigned depth,uint32_t *budget,const float min[3],const float max[3],DrStageTriangleFn fn,void *ctx,int *result){
 if(index>=count || depth>=64 || !*budget)return DR_STAGE_TREE;
 --*budget;const uint8_t *p=v->base+nodes+(size_t)index*32;
 for(unsigned i=0;i<3;i++)if(max[i]<fl(p+i*4) || fl(p+12+i*4)<min[i])return 0;
 uint32_t left=rd(p+24),right=rd(p+28);
 if(right==UINT32_MAX){
  if(left==UINT32_MAX)return 0;
  DrStageTriangle t;int e=triangle(v,m,left,&t);if(e)return e;
  *result|=fn(ctx,left,&t);return 0;
 }
 int e=query_node(v,m,nodes,count,left,depth+1,budget,min,max,fn,ctx,result);
 return e?e:query_node(v,m,nodes,count,right,depth+1,budget,min,max,fn,ctx,result);
}
int dr_stage_collision_query(const DrStageCollision *v,uint32_t zone,int record,
 const float min[3],const float max[3],DrStageTriangleFn fn,void *ctx,int *result){
 if(!result)return DR_STAGE_FORMAT;
 *result=0;
 if(!min || !max || !fn || !v || !v->base || zone>=v->zones || record< -1)return DR_STAGE_BOUNDS;
 for(unsigned i=0;i<3;i++)if(!isfinite(min[i]) || !isfinite(max[i]) || min[i]>max[i])return DR_STAGE_NUMBER;
 size_t z=v->zone_offset+(size_t)zone*64;if(z>v->size || v->size-z<64)return DR_STAGE_BOUNDS;
 const uint8_t *q=v->base+z;uint32_t word=rd(q+4);
 if(record>=0){size_t r;if((uint32_t)record>=rd(q+20) || array(v,rd(q+24),rd(q+20),16,&r))return DR_STAGE_BOUNDS;word=rd(v->base+r+(size_t)record*16+8);}
 if(!word)return 0;
 size_t m;if(span(v,word,24,&m))return DR_STAGE_BOUNDS;
 uint32_t n=rd(v->base+m);size_t nodes;if((int32_t)n<=0)return 0;
 if(array(v,rd(v->base+m+12),n,32,&nodes))return DR_STAGE_BOUNDS;
 uint32_t budget=n>UINT32_MAX/2?UINT32_MAX:n*2;
 return query_node(v,m,nodes,n,0,0,&budget,min,max,fn,ctx,result);
}
