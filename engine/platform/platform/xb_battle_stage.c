/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "port_compat.h"
#undef malloc
#include "xb_battle_stage.h"
#include "xb_stage_camera.h"
#include "xb_stage_effects.h"
#include "dr_container.h"
#include <float.h>
#include "battle/col_a.h"
#include "battle/eft_det_b.h"
#include "battle/stg_a.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
/* Preserve the original collision reader's stage view on host and PPC32.
 * The header fields not prepared here remain zero, except flags=NOT_READY. */
typedef struct NativeStage {
 unsigned char header[0x38];uint32_t zones_count;
 StgColZone *zones;StgBounds *bounds;uint32_t objects_count;StgColObj *objects;
} NativeStage;
_Static_assert(sizeof(StgColResult)==32,"collision result 64-bit masks ABI");
_Static_assert(offsetof(NativeStage,zones)==offsetof(StgColStage,zones),"collision stage zones ABI");
_Static_assert(offsetof(NativeStage,objects)==offsetof(StgColStage,objs),"collision stage objects ABI");
#if UINTPTR_MAX==UINT32_MAX
_Static_assert(sizeof(NativeStage)==sizeof(BtlStage),"native BtlStage ABI");
_Static_assert(sizeof(StgColZone)==64 && sizeof(StgColRec)==16 && sizeof(StgColObj)==80,"native zone/object ABI");
#endif
struct XbBattleStageCollision {
 DrStageCollision source;
 NativeStage stage;StgBounds bounds;ColMesh empty;
 uint8_t *arena;void *saved_base;int installed,querying;
 float starts[4][8];int has_starts;
 uint8_t *parameters,*path_bytes,*mef_bytes,*nav_bytes;
 void *saved_paths,*saved_mef;int parameters_bound;
 uint8_t *render_pack;size_t render_bytes,render_offset;void *render_texture,*saved_pack;int render_live,render_bound,render_error;void **animation_textures;unsigned animation_texture_count,animation_models,animation_nodes;
 unsigned surface_meshes,surface_triangles;
 XbStageGeometryInfo geometry;
};
extern s32 *gColMeshBase;
extern void *gStgColMesh;
extern s32 gStgColMode;
extern s32 StgCol_QueryZone(StgColResult *,StgColZone *,StgColBox *,void *,StgColCb);
extern void ColMesh_GetPolyVerts(ColMesh *,ColPoly *,ColVec *,ColVec *,ColVec *);
extern ColPoly *ColMesh_GetPoly(ColMesh *,s32);
static XbBattleStageCollision *active;
static void publish_parameters(XbBattleStageCollision *);
static void unpublish_parameters(XbBattleStageCollision *);
static uint32_t rd(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int range(const DrStageCollision *v,uint32_t word,size_t n,size_t *at){
 uint64_t off=(uint64_t)word*4;if(off>v->size || n>v->size-(size_t)off)return DR_STAGE_BOUNDS;*at=(size_t)off;return 0;
}
static void words(void *dest,const uint8_t *src,size_t n){
 for(size_t i=0;i<n;i+=4){uint32_t u=rd(src+i);memcpy((uint8_t *)dest+i,&u,4);}
}
static int native_mesh(XbBattleStageCollision *c,uint32_t word,void **out){
 if(!word){*out=&c->empty;return 0;}
 const DrStageCollision *v=&c->source;size_t at;if(range(v,word,24,&at))return DR_STAGE_BOUNDS;
 const uint8_t *h=v->base+at;uint32_t n=rd(h);words(c->arena+at,h,24);*out=c->arena+at;
 if((int32_t)n<=0)return 0;
 size_t nodes;if(n>v->size/32 || range(v,rd(h+12),(size_t)n*32,&nodes))return DR_STAGE_BOUNDS;
 if(nodes&15)return DR_STAGE_BOUNDS;
 words(c->arena+nodes,v->base+nodes,(size_t)n*32);
 uint32_t polys=0;
 for(uint32_t i=0;i<n;i++){
  const uint8_t *node=v->base+nodes+(size_t)i*32;
  if(rd(node+28)==UINT32_MAX && rd(node+24)!=UINT32_MAX){uint32_t poly=rd(node+24);if(poly>=v->size/32)return DR_STAGE_BOUNDS;if(poly+1>polys)polys=poly+1;}
 }
 size_t p;if(range(v,rd(h+16),(size_t)polys*32,&p))return DR_STAGE_BOUNDS;
 if(p&15)return DR_STAGE_BOUNDS;
 words(c->arena+p,v->base+p,(size_t)polys*32);
 uint32_t vertices=0;
 for(uint32_t i=0;i<polys;i++)for(unsigned j=0;j<3;j++){
  uint32_t index=rd(v->base+p+(size_t)i*32+4+j*4);if(index>=v->size/16)return DR_STAGE_BOUNDS;if(index+1>vertices)vertices=index+1;
 }
 size_t a;if(range(v,rd(h+20),(size_t)vertices*16,&a))return DR_STAGE_BOUNDS;
 if(a&15)return DR_STAGE_BOUNDS;
 words(c->arena+a,v->base+a,(size_t)vertices*16);return 0;
}
int XbBattleStage_FreeCollision(XbBattleStageCollision *c){
 if(!c)return 0;
 if(c->installed || c->querying || c->render_live)return XB_STAGE_STATE;
 XbBattleStage_FreeGeometry(c);
 if(c->stage.zones)for(uint32_t i=0;i<c->source.zones;i++){free(c->stage.zones[i].near);free(c->stage.zones[i].recs);}
 free(c->parameters);free(c->path_bytes);free(c->mef_bytes);free(c->nav_bytes);
 free(c->stage.zones);free(c->stage.objects);free(c->arena);free(c);return 0;
}
int XbBattleStage_PrepareCollision(const void *pack,size_t n,XbBattleStageCollision **out){
 if(!out)return DR_STAGE_FORMAT;
 *out=NULL;
 DrStageCollision v;int e=dr_stage_collision_open(pack,n,&v);if(e)return e;
 if(v.size>0x6cb800 || v.objects>63)return DR_STAGE_BOUNDS;
 XbBattleStageCollision *c=calloc(1,sizeof(*c));if(!c)return XB_STAGE_MEMORY;c->source=v;
 c->arena=calloc(1,v.size);c->stage.zones=calloc(v.zones,sizeof(*c->stage.zones));
 c->stage.objects=calloc(v.objects?v.objects:1,sizeof(*c->stage.objects));
 if(!c->arena || !c->stage.zones || !c->stage.objects){XbBattleStage_FreeCollision(c);return XB_STAGE_MEMORY;}
 c->stage.zones_count=v.zones;c->stage.objects_count=v.objects;
 uint32_t not_ready=1;memcpy(c->stage.header+8,&not_ready,4);
 c->bounds=(StgBounds){v.radius,v.top,v.bottom};c->stage.bounds=&c->bounds;
 for(uint32_t i=0;i<v.zones;i++){
  StgColZone *z=&c->stage.zones[i];const uint8_t *p=v.base+v.zone_offset+(size_t)i*64;
  z->nearCount=(s32)rd(p+8);z->recCount=(s32)rd(p+20);
  if(z->recCount>63){e=DR_STAGE_BOUNDS;goto fail;}
  z->near=calloc(z->nearCount?z->nearCount:1,sizeof(*z->near));z->recs=calloc(z->recCount?z->recCount:1,sizeof(*z->recs));
  if(!z->near || !z->recs){e=XB_STAGE_MEMORY;goto fail;}
  words(&z->center,p+32,16);words(&z->half,p+48,16);
  size_t a;if(range(&v,rd(p+12),(size_t)z->nearCount*4,&a)){e=DR_STAGE_BOUNDS;goto fail;}
  words(z->near,v.base+a,(size_t)z->nearCount*4);
  e=native_mesh(c,rd(p+4),&z->mesh);if(e)goto fail;
  if(range(&v,rd(p+24),(size_t)z->recCount*16,&a)){e=DR_STAGE_BOUNDS;goto fail;}
  for(s32 j=0;j<z->recCount;j++){
   StgColRec *r=&z->recs[j];const uint8_t *q=v.base+a+(size_t)j*16;r->broken=(s32)rd(q);r->obj=(s32)rd(q+4);r->unkC=(s32)rd(q+12);
   e=native_mesh(c,rd(q+8),&r->mesh);if(e)goto fail;
  }
 }
 /* No source byte or pointer is needed after preparation. */
 c->source.base=NULL;*out=c;return 0;
fail:XbBattleStage_FreeCollision(c);return e;
}
int XbBattleStage_InstallCollision(XbBattleStageCollision *c){
 if(!c || active || gBtlStage || c->installed)return XB_STAGE_STATE;
 c->saved_base=gColMeshBase;gColMeshBase=(s32 *)c->arena;gBtlStage=(BtlStage *)(void *)&c->stage;c->installed=1;active=c;publish_parameters(c);return 0;
}
int XbBattleStage_UninstallCollision(XbBattleStageCollision *c){
 if(!c || active!=c || !c->installed || c->querying || c->render_live || (void *)gBtlStage!=&c->stage)return XB_STAGE_STATE;
#if UINTPTR_MAX==UINT32_MAX
 if(c->nav_bytes && gStgNav && (void *)gStgNav->data==c->nav_bytes)return XB_STAGE_STATE;
#endif
 unpublish_parameters(c);gBtlStage=NULL;gColMeshBase=c->saved_base;c->installed=0;active=NULL;return 0;
}
typedef struct Query {DrStageTriangleFn fn;void *ctx;} Query;
static s32 triangle_callback(StgColNode *node,void *opaque){
 Query *q=opaque;ColPoly *p=ColMesh_GetPoly(gStgColMesh,node->poly);ColVec v[3];
 ColMesh_GetPolyVerts(gStgColMesh,p,&v[0],&v[1],&v[2]);
 DrStageTriangle t;t.flags=(uint32_t)p->flags;
 for(unsigned i=0;i<3;i++)memcpy(t.vertex[i],&v[i],16);
 memcpy(t.plane,&p->nrm,16);
 return q->fn(q->ctx,(uint32_t)node->poly,&t);
}
int XbBattleStage_QueryCollision(XbBattleStageCollision *c,uint32_t zone,const float min[3],const float max[3],DrStageTriangleFn fn,void *ctx,int *result){
 if(!result)return DR_STAGE_FORMAT;
 *result=0;
 if(!c || active!=c || !c->installed || c->querying || zone>=c->source.zones || !fn || !min || !max)return XB_STAGE_STATE;
 for(unsigned i=0;i<3;i++)if(!isfinite(min[i]) || !isfinite(max[i]) || min[i]>max[i])return DR_STAGE_NUMBER;
 StgColBox box;memcpy(box.min,min,12);memcpy(box.max,max,12);StgColResult r;Query q={fn,ctx};
 s32 count=gColWalkCount,limit=gColWalkMax;u16 *buf=gColWalkBuf;ColBox *old_box=gColWalkBox;ColMeshCb cb=gColWalkCb;void *old_ctx=gColWalkCtx;
 int mode=gStgColMode;void *mesh=gStgColMesh;c->querying=1;gStgColMode=0;
 *result=StgCol_QueryZone(&r,&c->stage.zones[zone],&box,&q,triangle_callback);
 gColWalkCount=count;gColWalkMax=limit;gColWalkBuf=buf;gColWalkBox=old_box;gColWalkCb=cb;gColWalkCtx=old_ctx;
 gStgColMode=mode;gStgColMesh=mesh;c->querying=0;return 0;
}

static float rf(const uint8_t *p){uint32_t v=rd(p);float f;memcpy(&f,&v,4);return f;}
int XbBattleStage_PrepareStarts(XbBattleStageCollision *c,const void *pack,size_t n){
 if(!c || !pack || c->installed || c->has_starts)return XB_STAGE_STATE;
 size_t at,bytes;unsigned flags;
 if(dr_pack_memory_entry(pack,n,1,&at,&bytes,&flags) || flags || bytes<168)return DR_STAGE_FORMAT;
 const uint8_t *p=(const uint8_t *)pack+at;
 if(rd(p)!=0x4450414d || rd(p+4))return DR_STAGE_FORMAT; /* MAPD, file-relative offsets */
 float native[4][8];size_t a[3],end[3];
 for(unsigned list=0;list<3;list++){
  unsigned count=list==0?2:1;
  if(rd(p+40+list*8)!=count)return DR_STAGE_FORMAT;
  a[list]=rd(p+44+list*8);size_t length=count*32;
  if(a[list]<168 || (a[list]&3) || a[list]>bytes || length>bytes-a[list])return DR_STAGE_BOUNDS;
  end[list]=a[list]+length;
  for(unsigned prev=0;prev<list;prev++)if(a[list]<end[prev] && a[prev]<end[list])return DR_STAGE_FORMAT;
  for(unsigned j=0;j<count;j++)for(unsigned k=0;k<8;k++){
   float f=rf(p+a[list]+j*32+k*4);if(!isfinite(f))return DR_STAGE_NUMBER;
   native[(list?list+1:0)+j][k]=(k%4==1 || k%4==2)?-f:f;
  }
 }
 memcpy(c->starts,native,sizeof(native));c->has_starts=1;return 0;
}
/* Same by-pointer ABI/layout as eft_det_a.h StgGroundOut/EftDetTri. */
typedef struct __attribute__((aligned(16))) SpawnGround {float x,y,z,w,nrm[4];int32_t flags;} SpawnGround;
extern s32 StgGround_TestTri(void *,void *,s32,f32);
static int spawn_triangle(void *ctx,uint32_t index,const DrStageTriangle *t){
 (void)index;if((t->flags&0x08000000u) || !(t->plane[1]<0))return 0;
 float tri[16] __attribute__((aligned(16)));memcpy(tri,t->vertex,48);memcpy(tri+12,t->plane,16);
 return StgGround_TestTri(ctx,tri,(s32)t->flags,-701.0f)!=0;
}
int XbBattleStage_GetPlacement(XbBattleStageCollision *c,unsigned side,unsigned kind,float pos[4],float rot[4],int *zone){
 if(!pos || !rot || !zone)return DR_STAGE_FORMAT;
 memset(pos,0,16);memset(rot,0,16);*zone=-1;
 if(!c || active!=c || !c->installed || !c->has_starts || c->querying || side>1 || kind>2)return XB_STAGE_STATE;
 const float *p=c->starts[kind?kind+1:side];int selected=-1;
 for(uint32_t i=0;i<c->source.zones;i++){
  const StgColZone *z=&c->stage.zones[i];
  if(p[0]>=z->center.x-z->half.x && p[0]<=z->center.x+z->half.x && p[2]>=z->center.z-z->half.z && p[2]<=z->center.z+z->half.z){selected=(int)i;break;}
 }
 if(selected<0)return DR_STAGE_BOUNDS;
 SpawnGround g={p[0],FLT_MAX,p[2],1,{0,0,0,0},0};
 float lo[3]={g.x-1,-701,g.z-1},hi[3]={g.x+1,7301,g.z+1};int result;
 int e=XbBattleStage_QueryCollision(c,(unsigned)selected,lo,hi,spawn_triangle,&g,&result);if(e)return e;
 if(g.y==FLT_MAX || !isfinite(g.y))return DR_STAGE_BOUNDS;
 float dx=p[0]-p[4],dz=p[2]-p[6],length=sqrtf(dx*dx+1+dz*dz);
 if(!isfinite(length) || length==0)return DR_STAGE_NUMBER;
 float yaw=atan2f(dx/length,dz/length)-3.14159265f;
 if(yaw>3.14159265f)yaw-=6.2831853f;
 if(yaw< -3.14159265f)yaw+=6.2831853f;
 pos[0]=g.x;pos[1]=g.y;pos[2]=g.z;pos[3]=1;rot[1]=yaw;rot[3]=1;*zone=selected;return 0;
}
int XbStage_TryStartPlace(int side,void *pos,void *rot,int alt,int *zone){
 if(!active || (void *)gBtlStage!=&active->stage)return 0;
 int z=-1,e=XbBattleStage_GetPlacement(active,(unsigned)side,alt==1?1:alt==-1?2:0,pos,rot,&z);
 if(zone)*zone=e?-1:z;
 return 1;
}

#include "xb_stage_parameters.inc"

#include "xb_stage_geometry.inc"
