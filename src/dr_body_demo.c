/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "dr_body_demo.h"
#include "dr_texfile.h"
#include "dr_container.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
enum {VERTICES=32766,FACE_VERTICES=8190};
typedef struct FaceCache {DrGsVertex*v;unsigned char*ids;size_t n;DrBodyPipelineStats stats;} FaceCache;
static DrBodySource*source;
static DrGsVertex *mesh,*base_mesh,*work_mesh;
static unsigned char *material_ids,*base_ids,*work_ids;
static size_t count,base_count;
static DrBodyFaceSpan face_span;
static uint32_t materials[14],previews[2],face_mask;
static uint64_t material_keys[14];
static unsigned material_count;
static float scale,cx,cy;
static FaceCache faces[8];
static DrBodyPipelineStats base_stats;
static DrBodyDemoProfile profile;
static DrBodyDemoProbe probes[2];
static uint64_t now_us(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000000+(uint64_t)t.tv_nsec/1000;}
static uint64_t elapsed(uint64_t start){uint64_t end=now_us();return end>=start?end-start:0;}
static void transform(DrGsVertex*v,size_t n){for(size_t i=0;i<n;i++){v[i].x=640-(v[i].x-cx)*scale;v[i].y=375+(v[i].y-cy)*scale;v[i].z=1-v[i].z;}}
static void clear_owned(void){
 free(base_mesh);free(work_mesh);free(base_ids);free(work_ids);dr_body_source_free(source);
 for(unsigned i=0;i<8;i++){free(faces[i].v);free(faces[i].ids);}memset(faces,0,sizeof(faces));
 mesh=base_mesh=work_mesh=NULL;material_ids=base_ids=work_ids=NULL;source=NULL;
 count=base_count=0;material_count=face_mask=0;memset(probes,0,sizeof(probes));previews[0]=previews[1]=0;
}
int dr_body_demo_close(void){int rc=dr_xenos_close();if(rc)return rc;clear_owned();return 0;}
static int entry(unsigned id,const unsigned char**p,size_t*n){size_t bytes,at;unsigned flags;const unsigned char*pack=dr_body_source_pack(source,&bytes);int e=dr_pack_memory_entry(pack,bytes,id,&at,n,&flags);if(e)return e;if(flags)return DR_XENOS_INVALID;*p=pack+at;return 0;}
static int map_materials(const uint64_t*keys,size_t n,unsigned char*ids){
 uint64_t wanted[14];unsigned number=material_count;memcpy(wanted,material_keys,number*sizeof(*wanted));
 for(size_t i=0;i<n;i++){
  unsigned j=0;while(j<number&&wanted[j]!=keys[i])j++;
  if(j==number){if(number==14)return DR_XENOS_FULL;wanted[number++]=keys[i];}ids[i]=(unsigned char)j;
 }
 if(number==material_count)return 0;
 const unsigned char*p;size_t bytes;int e=entry(11,&p,&bytes);if(e)return e;
 unsigned char*rgba=malloc(4*1024*1024);if(!rgba)return DR_XENOS_FULL;
 while(!e&&material_count<number){
  unsigned i=material_count;DrTexFileImage image;uint32_t index;
  e=dr_texfile_material(p,bytes,wanted[i],&index);
  if(!e)e=dr_texfile_decode(p,bytes,index,rgba,4*1024*1024,&image);
  if(!e)e=dr_xenos_texture_create(image.width,image.height,rgba,&materials[i]);
  if(!e){material_keys[i]=wanted[i];material_count++;profile.texture_uploads++;}
 }
 free(rgba);return e;
}
static void color_probe(unsigned index,const unsigned char*rgba,const DrTexFileImage*image){
 /* Select a nonwhite skin-like pixel at the EXACT nearest-sampled screen location. */
 for(unsigned y=40;y<120;y++)for(unsigned x=40;x<120;x++){
  unsigned tx=((2*x+1)*image->width)/320,ty=((2*y+1)*image->height)/320;
  const unsigned char*p=rgba+((size_t)ty*image->width+tx)*4;
  if(p[3]>0&&p[0]>100&&p[1]<235&&p[1]>50&&p[0]>p[1]+10&&p[1]>p[2]+10){
   if(!tx||!ty||tx+1>=image->width||ty+1>=image->height)continue;
   int flat=1;for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++)if(memcmp(p,rgba+((size_t)(ty+dy)*image->width+tx+dx)*4,4))flat=0;
   if(!flat)continue; /* Avoid texel edges and half-pixel convention ambiguity. */
   unsigned a=p[3]>128?128:p[3];const unsigned bg[3]={13,15,28};uint32_t expected=0xff000000u;
   for(unsigned k=0;k<3;k++)expected|=((p[k]*a+bg[k]*(128-a)+64)/128)<<(16-8*k);
   probes[index]=(DrBodyDemoProbe){.x=950+x,.y=210+index*190+y,.argb=expected,.rgba=(uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]};return;
  }
 }
}
int dr_body_demo_select_face(int face,DrBodyPipelineStats*stats){
 if(!source||!base_mesh||!stats||face< -1||face>7||(face>=0&&!(face_mask&(1u<<(unsigned)face))))return DR_XENOS_INVALID;
 uint64_t start=now_us();profile.face_cache_hit=face<0||(faces[face].v!=NULL);
 if(face<0){mesh=base_mesh;material_ids=base_ids;count=base_count;*stats=base_stats;profile.face_vertices=(unsigned)face_span.count;profile.expression_us=elapsed(start);return 0;}
 if(!face_span.present)return DR_XENOS_INVALID;
 FaceCache*f=&faces[face];
 if(!f->v){
  DrGsVertex*v=malloc(FACE_VERTICES*sizeof(*v));unsigned char*ids=malloc(FACE_VERTICES/3);uint64_t*keys=malloc(FACE_VERTICES/3*sizeof(*keys));
  if(!v||!ids||!keys){free(v);free(ids);free(keys);return DR_XENOS_FULL;}
  size_t n=0;DrBodyPipelineStats info;profile.face_builds++;
  int e=dr_body_source_build(source,v,FACE_VERTICES,keys,FACE_VERTICES/3,face,1,&n,&info,NULL);
  if(!e&&base_count-face_span.count+n>VERTICES)e=DR_XENOS_FULL;
  if(!e)e=map_materials(keys,n/3,ids);
  free(keys);
  if(e){free(v);free(ids);return e;}
  transform(v,n);f->v=v;f->ids=ids;f->n=n;f->stats=info;
 }
 if(!work_mesh){
  work_mesh=malloc(VERTICES*sizeof(*work_mesh));work_ids=malloc(VERTICES/3);
  if(!work_mesh||!work_ids){free(work_mesh);free(work_ids);work_mesh=NULL;work_ids=NULL;return DR_XENOS_FULL;}
 }
 size_t prefix=face_span.first,suffix=base_count-prefix-face_span.count;
 memcpy(work_mesh,base_mesh,prefix*sizeof(*work_mesh));memcpy(work_ids,base_ids,prefix/3);
 memcpy(work_mesh+prefix,f->v,f->n*sizeof(*work_mesh));memcpy(work_ids+prefix/3,f->ids,f->n/3);
 memcpy(work_mesh+prefix+f->n,base_mesh+prefix+face_span.count,suffix*sizeof(*work_mesh));
 memcpy(work_ids+(prefix+f->n)/3,base_ids+(prefix+face_span.count)/3,suffix/3);
 mesh=work_mesh;material_ids=work_ids;count=prefix+f->n+suffix;*stats=f->stats;
 profile.face_vertices=(unsigned)f->n;profile.expression_us=elapsed(start);return 0;
}
int dr_body_demo_open_face(const dr_disc*disc,int face,DrBodyPipelineStats*stats){
 if(source||base_mesh)return DR_XENOS_STATE;
 if(!stats||face< -1||face>7)return DR_XENOS_INVALID;
 memset(&profile,0,sizeof(profile));uint64_t total=now_us(),at=total;
 int e=dr_body_source_open(disc,"pzs3us1/01423.bin",&source);profile.source_us=elapsed(at);
 if(e)return e;
 dr_body_source_pack(source,&profile.source_bytes);profile.model_reads=1;
 base_mesh=malloc(VERTICES*sizeof(*base_mesh));base_ids=malloc(VERTICES/3);uint64_t*keys=malloc(VERTICES/3*sizeof(*keys));
 if(!base_mesh||!base_ids||!keys){free(keys);clear_owned();return DR_XENOS_FULL;}
 at=now_us();profile.body_builds=1;
 e=dr_body_source_build(source,base_mesh,VERTICES,keys,VERTICES/3,-1,0,&base_count,&base_stats,&face_span);
 profile.body_us=elapsed(at);
 if(e){free(keys);clear_owned();return e;}
 float xmin=INFINITY,ymin=INFINITY,xmax=-INFINITY,ymax=-INFINITY;
 for(size_t i=0;i<base_count;i++){xmin=fminf(xmin,base_mesh[i].x);xmax=fmaxf(xmax,base_mesh[i].x);ymin=fminf(ymin,base_mesh[i].y);ymax=fmaxf(ymax,base_mesh[i].y);}
 float extent=fmaxf(xmax-xmin,ymax-ymin);
 if(!isfinite(extent)||extent<=0){free(keys);clear_owned();return DR_XENOS_INVALID;}
 scale=470/extent;cx=(xmin+xmax)*.5f;cy=(ymin+ymax)*.5f;transform(base_mesh,base_count);
 /* Retained base geometry uses actual size; the larger workspace is only needed for expression changes. */
 DrGsVertex*small=realloc(base_mesh,base_count*sizeof(*base_mesh));if(small)base_mesh=small;
 unsigned char*small_ids=realloc(base_ids,base_count/3);if(small_ids)base_ids=small_ids;
 e=dr_xenos_open();if(e){free(keys);clear_owned();return e;}
 at=now_us();e=map_materials(keys,base_count/3,base_ids);free(keys);
 unsigned char*rgba=NULL;const unsigned char*p;size_t bytes;
 if(!e){rgba=malloc(4*1024*1024);if(!rgba)e=DR_XENOS_FULL;}
 if(!e)e=entry(0,&p,&bytes);
 for(unsigned i=0;i<2&&!e;i++){
  DrTexFileImage image;e=dr_texfile_decode(p,bytes,i,rgba,4*1024*1024,&image);
  if(!e){color_probe(i,rgba,&image);e=dr_xenos_texture_create(image.width,image.height,rgba,&previews[i]);if(!e)profile.texture_uploads++;}
 }
 free(rgba);profile.texture_us=elapsed(at);face_mask=base_stats.face_mask;
 if(!e)e=dr_body_demo_select_face(face,stats);
 if(e){int close=dr_xenos_close();if(!close)clear_owned();return e;}
 profile.total_us=elapsed(total);return 0;
}
int dr_body_demo_draw(uint32_t width,uint32_t height)
{
    if(!mesh)return DR_XENOS_STATE;
    const DrXenosDraw draw={.logical_width=1280,.logical_height=720,.depth=2,.depth_write=1,.blend=1,
        .scissor={0,0,(int)width,(int)height}};
    /* The arena is 1024 vertices: end/fence/wait before every reuse. No resolves
     * between chunks, preserving color and depth in eDRAM. This is deliberately
     * a slow diagnostic path, not a measured battle rendering implementation. */
    for(size_t at=0;at<count;) {
        uint32_t n=(uint32_t)(count-at);if(n>1023)n=1023;
        int rc=dr_xenos_begin(width,height);if(rc)return rc;
        for(size_t at_run=at;at_run<at+n && !rc;) {
            size_t end_run=at_run+3;unsigned material=material_ids[at_run/3];
            while(end_run<at+n && material_ids[end_run/3]==material)end_run+=3;
            rc=dr_xenos_draw(mesh+at_run,(uint32_t)(end_run-at_run),materials[material],&draw);
            at_run=end_run;
        }
        int end=dr_xenos_end();
        if(rc || end)return rc?rc:end;
        at+=n;
    }
    int rc=dr_xenos_begin(width,height);if(rc)return rc;
    DrXenosDraw image_draw=draw;image_draw.depth=1;image_draw.depth_write=0;image_draw.blend=1;
    for(unsigned i=0;i<2 && !rc;i++) {
        float x=950,y=210+(float)i*190,w=160,h=160;
        const DrGsVertex quad[]={
            {x,y,.5f,128,128,128,128,0,0,1},{x+w,y,.5f,128,128,128,128,1,0,1},{x,y+h,.5f,128,128,128,128,0,1,1},
            {x,y+h,.5f,128,128,128,128,0,1,1},{x+w,y,.5f,128,128,128,128,1,0,1},{x+w,y+h,.5f,128,128,128,128,1,1,1}};
        rc=dr_xenos_draw(quad,6,previews[i],&image_draw);
    }
    int end=dr_xenos_end();return rc?rc:end;
}

unsigned dr_body_demo_material_count(void){return material_count;}
uint32_t dr_body_demo_face_mask(void){return face_mask;}
int dr_body_demo_open(const dr_disc*d,DrBodyPipelineStats*s){return dr_body_demo_open_face(d,-1,s);}
int dr_body_demo_next_face(int current){if(!mesh||current< -1||current>7)return -1;for(unsigned i=0;i<9;i++){current=(current+2)%9-1;if(current<0||(face_mask&(1u<<(unsigned)current)))return current;}return -1;}
void dr_body_demo_profile(DrBodyDemoProfile*out){if(out)*out=profile;}
int dr_body_demo_probe(unsigned index,DrBodyDemoProbe*out){if(!out||index>=2||!probes[index].argb)return DR_XENOS_INVALID;*out=probes[index];return 0;}
