/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Viewer batching/ownership with real native core and mocked GPU calls. */
#define main previous_native_tests
#include "test_xenos.c"
#undef main
#include "dr_body_demo.h"
#include "dr_texfile.h"
static int preview_error;
int dr_texfile_model_image(const dr_disc *d,const char *path,uint32_t index,void *rgba,size_t cap,DrTexFileImage *info)
{
    assert(d && path && index<2 && cap>=16384);if(index==1 && preview_error)return preview_error;
    memset(rgba,128,16384);
    *info=(DrTexFileImage){64,64,19,16384};return 0;
}
int dr_texfile_body_material(const dr_disc *d,const char *path,uint64_t key,void *rgba,size_t cap,DrTexFileImage *info,uint32_t *index)
{
    assert(d && path && key<2 && cap>=16384);memset(rgba,128,16384);
    *index=(uint32_t)key;*info=(DrTexFileImage){64,64,20,16384};return 0;
}
static int load_error;
int dr_body_pipeline_build(const dr_disc *d,const char *path,DrGsVertex *v,size_t cap,size_t *n,DrBodyPipelineStats *s)
{
    assert(d && !strcmp(path,"pzs3us1/01423.bin") && cap>=1026);
    *n=0;memset(s,0,sizeof(*s));if(load_error)return load_error;
    for(unsigned i=0;i<1026;i++)v[i]=(DrGsVertex){.x=(float)(i%3),.y=(float)(i%2),.z=.5f,
        .r=128,.g=128,.b=128,.a=128,.q=1};
    *n=1026;s->face_mask=31;return 0;
}
int dr_body_pipeline_materials(const dr_disc *d,const char *path,DrGsVertex *v,size_t cap,uint64_t *keys,size_t kcap,size_t *n,DrBodyPipelineStats *s)
{
    int rc=dr_body_pipeline_build(d,path,v,cap,n,s);if(rc)return rc;
    assert(kcap>=*n/3);
    for(size_t i=0;i<*n/3;i++)keys[i]=i==1?1:0; /* A/B/A material runs across the chunk boundary. */
    return 0;
}
int main(void)
{
    previous_native_tests();dr_disc disc={0};DrBodyPipelineStats stats;
    load_error=-280;assert(dr_body_demo_open(&disc,&stats)==-280);load_error=0;
    unsigned released=texture_frees;preview_error=-301;
    assert(dr_body_demo_open(&disc,&stats)==-301 && texture_frees==released+3);preview_error=0;
    released=texture_frees;texture_failure=texture_creates+2;
    assert(dr_body_demo_open(&disc,&stats)==DR_XENOS_GPU && texture_frees==released+1);texture_failure=0;
    assert(!dr_body_demo_open(&disc,&stats));
    assert(dr_body_demo_material_count()==2);
    assert(dr_body_demo_face_mask()==31 && dr_body_demo_next_face(-1)==0 && dr_body_demo_next_face(4)==-1);
    assert(dr_body_demo_open(&disc,&stats)==DR_XENOS_STATE);
    unsigned before=draws,w=waits,f=fences;
    assert(!dr_body_demo_draw(1280,720));
    assert(draws==before+6 && waits==w+2 && fences==f+3); /* Four material runs + two preview quads. */
    assert(dr_body_demo_draw(641,720)==DR_XENOS_INVALID);
    unsigned a=freed,t=texture_frees;wait_result=STATUS_IO_TIMEOUT;
    assert(dr_body_demo_draw(1280,720)==DR_XENOS_GPU && draws==before+6);
    assert(dr_body_demo_close()==DR_XENOS_GPU && freed==a && texture_frees==t);
    wait_result=STATUS_SUCCESS;assert(!dr_body_demo_close());
    assert(!dr_body_demo_open(&disc,&stats) && !dr_body_demo_close());
    assert(dr_body_demo_draw(1280,720)==DR_XENOS_STATE);
    puts("PASS body viewer: 1023-vertex chunks, fence before arena reuse, GPU timeout preserves ownership, reopen after close; no GPU execution");
}

int dr_body_pipeline_face(const dr_disc *d,const char *p,DrGsVertex *v,size_t cap,uint64_t *k,size_t kcap,int face,size_t *n,DrBodyPipelineStats *s)
{assert(face>=-1 && face<8);return dr_body_pipeline_materials(d,p,v,cap,k,kcap,n,s);}

/* Memory-source contract for the cache-aware viewer; real formats/VU are covered
 * separately by body_cache_real.c, not by these synthetic ownership fixtures. */
struct DrBodySource {unsigned unused;};
static struct DrBodySource cached_source;
static unsigned char cached_pack[32];
int dr_body_source_open(const dr_disc*d,const char*p,DrBodySource**out){assert(d&&p&&out);*out=NULL;if(load_error)return load_error;*out=&cached_source;return 0;}
void dr_body_source_free(DrBodySource*s){assert(!s||s==&cached_source);}
const void*dr_body_source_pack(const DrBodySource*s,size_t*n){assert(s==&cached_source&&n);*n=sizeof(cached_pack);return cached_pack;}
int dr_body_source_build(const DrBodySource*s,DrGsVertex*v,size_t cap,uint64_t*k,size_t kc,int face,int only,size_t*n,DrBodyPipelineStats*st,DrBodyFaceSpan*span){
 assert(s==&cached_source);int rc=dr_body_pipeline_face((dr_disc*)&cached_source,"pzs3us1/01423.bin",v,cap,k,kc,face,n,st);
 if(!rc&&only)*n=3;
 if(span)*span=(DrBodyFaceSpan){.first=3,.count=3,.present=1};
 return rc;
}
int dr_pack_memory_entry(const void*p,size_t n,uint32_t index,size_t*at,size_t*bytes,unsigned*flags){assert(p==cached_pack&&n==32&&(index==0||index==11));*at=index==0?0:16;*bytes=16;*flags=0;return 0;}
int dr_texfile_material(const void*p,size_t n,uint64_t key,uint32_t*index){assert(p==cached_pack+16&&n==16&&key<2);*index=(uint32_t)key;return 0;}
int dr_texfile_decode(const void*p,size_t n,uint32_t index,void*rgba,size_t cap,DrTexFileImage*info){assert(n==16);
 if(p==cached_pack)return dr_texfile_model_image((dr_disc*)&cached_source,"pzs3us1/01423.bin",index,rgba,cap,info);
 uint32_t found;assert(p==cached_pack+16);return dr_texfile_body_material((dr_disc*)&cached_source,"pzs3us1/01423.bin",index,rgba,cap,info,&found);
}
