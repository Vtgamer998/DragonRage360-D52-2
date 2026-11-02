/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Program and constant layout: src/sys/vu1_packet.c and src/vu1/prog0.vsm;
 * GIF packed layout: port/src/gs/gs_core.c, revision 3ca419c7966b11438e2dc3c9a7e1914f102ed545.
 */
#include "dr_body_pipeline.h"
#include "dr_body_model.h"
#include "dr_container.h"
#include "dr_elf.h"
#include "dr_face_vif.h"
#include "dr_vu1.h"
#include "dr_gs_stream.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
enum {PIPE_FORMAT=-280,PIPE_CAPACITY=-281,PIPE_MEMORY=-282};
static uint32_t word(const unsigned char *p) {return dr_resource_le32(p);}
static uint64_t wide(const unsigned char *p) {return word(p)|(uint64_t)word(p+4)<<32;}
static void put(unsigned char *p,uint32_t v) {for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static void vector(unsigned char *p,unsigned q,float x,float y,float z,float w)
{
    const float f[4]={x,y,z,w};for(unsigned i=0;i<4;i++) {uint32_t bits;memcpy(&bits,&f[i],4);put(p+q*16+i*4,bits);}
}
typedef struct {
    DrGsVertex *vertices;size_t used,capacity;int error;
    const unsigned char *chain;DrBodyPipelineStats stats;unsigned part;
    uint64_t *tex0;size_t material_capacity;
    DrGsStream stream;
} Builder;
static void gif(void *opaque,const unsigned char *data,uint32_t qwc)
{
    Builder *b=opaque;if(b->error)return;b->stats.gif_packets++;
    uint32_t at=0,prim=0;DrGsVertex ring[2];unsigned count=0;
    uint64_t texture=0;int have_texture=0;float stq[3]={0,0,1};
    while(at<qwc) {
        uint64_t tag=wide(data+at*16),regs=wide(data+at*16+8);at++;
        uint32_t n=(uint32_t)tag&32767,nreg=(uint32_t)(tag>>60)&15;
        if(!nreg)nreg=16;
        if(((tag>>58)&3)!=0 || (uint64_t)n*nreg>qwc-at) {b->error=PIPE_FORMAT;return;}
        if((tag>>46)&1) {prim=(uint32_t)(tag>>47)&2047;count=0;if(!(prim&512))b->stats.strips++;}
        for(uint32_t i=0;i<n;i++)for(uint32_t r=0;r<nreg;r++) {
            const unsigned char *v=data+at++*16;unsigned reg=(unsigned)(regs>>(r*4))&15;
            if(reg==0) {prim=word(v)&2047;count=0;continue;}
            if(reg==2) {
                for(unsigned j=0;j<3;j++) {uint32_t bits=word(v+j*4);memcpy(&stq[j],&bits,4);}
                if(!isfinite(stq[0]) || !isfinite(stq[1]) || !isfinite(stq[2]) || stq[2]==0) {b->error=PIPE_FORMAT;return;}
                continue;
            }
            if(reg==14) {if(wide(v+8)==6) {texture=wide(v);have_texture=1;}continue;}
            if(reg==1 || reg==15)continue;
            if(reg!=4) {b->error=PIPE_FORMAT;return;}
            if(prim&512)continue; /* Shading-ramp layer is not part of this diagnostic. */
            if((prim&7)!=4) {b->error=PIPE_FORMAT;return;}
            unsigned adc=(word(v+12)>>15)&1;
            unsigned color=b->part*2654435761u;
            DrGsVertex point={.x=(word(v)&65535)/16.0f,.y=(word(v+4)&65535)/16.0f,
                .z=((word(v+8)>>4)&0xffffff)/16777215.0f,
                .r=(uint8_t)(48+(color&63)),.g=(uint8_t)(48+((color>>8)&63)),
                .b=(uint8_t)(48+((color>>16)&63)),.a=128,.s=stq[0],.t=stq[1],.q=stq[2]};
            if(b->tex0)point.r=point.g=point.b=128;
            if(adc)b->stats.adc_vertices++;
            if(count>=2 && !adc) {
                if(b->capacity-b->used<3) {b->error=PIPE_CAPACITY;return;}
                if(b->tex0) {
                    if(!have_texture) {b->error=PIPE_FORMAT;return;}
                    if(((texture>>35)&3) || !(texture&(UINT64_C(1)<<34))) {b->error=PIPE_FORMAT;return;}
                    if(b->stats.triangles>=b->material_capacity) {b->error=PIPE_CAPACITY;return;}
                    b->tex0[b->stats.triangles]=texture;
                }
                /* Preserve strip winding: ring slots exchange chronological
                 * order every vertex, which supplies the alternating winding. */
                b->vertices[b->used++]=ring[0];b->vertices[b->used++]=ring[1];
                b->vertices[b->used++]=point;b->stats.triangles++;
            }
            ring[count&1]=point;count++;
        }
    }
}
static int stream_gif(void *opaque,const unsigned char *data,uint32_t qwc)
{Builder *b=opaque;gif(b,data,qwc);return b->error;}
static int command(DrGsStream *stream,uint32_t code,const void *data,size_t bytes)
{
    unsigned char header[4];put(header,code);
    int rc=dr_gs_stream_vif(stream,header,sizeof(header));
    if(!rc && bytes)rc=dr_gs_stream_vif(stream,data,bytes);
    return rc;
}
static int commands(void *opaque,const DrFaceVifCommand *c)
{
    Builder *b=opaque;
    if(c->command==0x17)b->stats.mscnt++;
    int rc=command(&b->stream,c->code,b->chain+c->data_offset,c->data_bytes);
    if(rc)b->error=rc;
    return b->error;
}
static int program(DrGsStream *stream,const unsigned char *p,size_t size,uint32_t *instructions)
{
    size_t at=0;*instructions=0;
    while(at<size) {
        if(size-at<4)return PIPE_FORMAT;
        uint32_t code=word(p+at);at+=4;unsigned cmd=(code>>24)&127,n=(code>>16)&255;
        if(!cmd)continue;
        if(cmd!=0x4a)return PIPE_FORMAT;
        size_t bytes=(size_t)(n?n:256)*8;if(bytes>size-at)return PIPE_FORMAT;
        int rc=command(stream,code,p+at,bytes);if(rc)return rc;
        *instructions+=(uint32_t)(bytes/8);at+=bytes;
    }
    return *instructions==127?0:PIPE_FORMAT;
}
struct DrBodySource {
    unsigned char *bytes;size_t size,model_offset;
    DrBodyPart parts[256];DrBodyModel model;
    unsigned char microcode[0x400];uint32_t face_mask;
};
void dr_body_source_free(DrBodySource*s){if(s){free(s->bytes);free(s);}}
const void *dr_body_source_pack(const DrBodySource*s,size_t*n){if(n)*n=s?s->size:0;return s?s->bytes:NULL;}
int dr_body_source_open(const dr_disc*disc,const char*path,DrBodySource**out){
    if(!out)return PIPE_FORMAT;
    *out=NULL;if(!disc||!path)return PIPE_FORMAT;
    DrBodySource*s=calloc(1,sizeof(*s));if(!s)return PIPE_MEMORY;
    dr_span pack,elf,micro;int rc=dr_game_file(disc,path,&pack);size_t offset=0,size=0,bad;
    if(!rc&&(!pack.size||pack.size>16*1024*1024))rc=PIPE_FORMAT;
    if(!rc){s->size=(size_t)pack.size;s->bytes=malloc(s->size);if(!s->bytes)rc=PIPE_MEMORY;}
    if(!rc)rc=dr_read(disc,&pack,0,s->bytes,s->size);
    if(!rc)rc=dr_pack_memory_entry(s->bytes,s->size,2,&offset,&size,NULL);
    if(!rc)rc=dr_body_model_read(s->bytes+offset,size,s->parts,256,&s->model,&bad);
    s->model_offset=offset;
    if(!rc)for(unsigned i=0;i<8;i++){
        size_t at,n;int e=dr_pack_memory_entry(s->bytes,s->size,3+i,&at,&n,NULL);
        if(!e)s->face_mask|=1u<<i;else if(e!=DR_NOT_FOUND){rc=e;break;}
    }
    if(!rc)rc=dr_find(disc,"SLUS_216.78",&elf);
    if(!rc)rc=dr_elf_mips_span(disc,&elf,0x2bf6b0,0x400,&micro);
    if(!rc)rc=dr_read(disc,&micro,0,s->microcode,sizeof(s->microcode));
    if(rc){dr_body_source_free(s);return rc;}
    *out=s;return 0;
}
int dr_body_source_build(const DrBodySource*source,DrGsVertex*vertices,size_t capacity,
                        uint64_t*tex0,size_t material_capacity,int face,int only_face,
                        size_t*written,DrBodyPipelineStats*stats,DrBodyFaceSpan*span){
    if(written)*written=0;
    if(stats)memset(stats,0,sizeof(*stats));
    if(span)memset(span,0,sizeof(*span));
    if(!source||!vertices||!capacity||!written||!stats||face< -1||face>7||(only_face!=0&&only_face!=1))return PIPE_FORMAT;
    unsigned char*bytes=source->bytes;size_t offset=source->model_offset;
    const DrBodyPart*parts=source->parts;DrBodyModel model=source->model;
    const unsigned char*microcode=source->microcode;
    Builder b={.vertices=vertices,.capacity=capacity,.tex0=tex0,.material_capacity=material_capacity};
    b.stats.face_mask=source->face_mask;
    size_t face_offset=0,face_bytes=0,bad;int rc=0;
    if(face>=0)rc=dr_pack_memory_entry(bytes,source->size,3+(unsigned)face,&face_offset,&face_bytes,NULL);
    if(!rc&&face>=0)rc=dr_face_vif_parse(bytes+face_offset,face_bytes,NULL,NULL,NULL,&bad);
    if(!rc)for(unsigned i=0;i<model.parts;i++) {
        const DrBodyPart *part=&parts[i];if(!part->chain_bytes || !part->enabled || (only_face && part->node!=0x30))continue;
        size_t part_start=b.used;
        unsigned char constants[34*16]={0};
        for(unsigned axis=0;axis<3;axis++) {
            vector(constants,axis,axis==0,axis==1,axis==2,0);
            vector(constants,4+axis,axis==0,axis==1,axis==2,0);
        }
        vector(constants,3,part->pivot_a[0],part->pivot_a[1],part->pivot_a[2],1);
        vector(constants,7,part->pivot_b[0],part->pivot_b[1],part->pivot_b[2],1);
        vector(constants,8,part->pivot_a[0],part->pivot_a[1],part->pivot_a[2],part->pivot_a[3]);
        vector(constants,9,part->pivot_b[0],part->pivot_b[1],part->pivot_b[2],part->pivot_b[3]);
        for(unsigned axis=0;axis<4;axis++)vector(constants,10+axis,axis==0,axis==1,axis==2,axis==3);
        vector(constants,14,2.8284271f,0,-7071.0678f,0);
        vector(constants,15,0,4,0,0);vector(constants,16,2.8284271f,0,7071.0678f,0);
        vector(constants,17,640,600,8388608,1);
        vector(constants,18,.003f,0,0,0);vector(constants,19,0,.003f,0,0);
        vector(constants,20,0,0,.003f,0);vector(constants,21,0,0,0,1);
        vector(constants,22,128,128,128,128);vector(constants,23,128,128,128,128);
        size_t chain_bytes=part->chain_bytes;
        b.part=part->node;b.chain=bytes+offset+part->chain_offset;
        if(face>=0 && part->node==0x30) {b.chain=bytes+face_offset;chain_bytes=face_bytes;}
        rc=dr_gs_stream_init(&b.stream,stream_gif,&b);if(rc)break;
        rc=program(&b.stream,microcode,sizeof(source->microcode),&b.stats.program_instructions);if(rc)break;
        rc=command(&b.stream,0x6c220000,constants,sizeof(constants));if(rc)break;
        rc=command(&b.stream,0x03000022,NULL,0);if(rc)break;
        rc=command(&b.stream,0x020001ef,NULL,0);if(rc)break;
        rc=command(&b.stream,0x14000000,NULL,0);if(rc)break;
        rc=dr_face_vif_parse(b.chain,chain_bytes,commands,&b,NULL,&bad);
        if(!rc)rc=dr_gs_stream_finish(&b.stream);
        if(b.error) {rc=b.error;}if(rc)break;
        if(part->node==0x30 && span){span->present=1;span->first=part_start;span->count=b.used-part_start;}
        b.stats.body_chains++;
    }

    if(rc)return rc;
    b.stats.parts=only_face?b.stats.body_chains:model.parts;*stats=b.stats;*written=b.used;return b.used?0:PIPE_FORMAT;
}
static int build(const dr_disc*disc,const char*path,DrGsVertex*vertices,size_t capacity,
                 uint64_t*tex0,size_t material_capacity,int face,size_t*written,DrBodyPipelineStats*stats){
    if(written)*written=0;
    if(stats)memset(stats,0,sizeof(*stats));
    if(!disc||!path||!vertices||!capacity||!written||!stats||face< -1||face>7)return PIPE_FORMAT;
    DrBodySource*s=NULL;int rc=dr_body_source_open(disc,path,&s);
    if(!rc)rc=dr_body_source_build(s,vertices,capacity,tex0,material_capacity,face,0,written,stats,NULL);
    dr_body_source_free(s);return rc;
}
int dr_body_pipeline_build(const dr_disc *d,const char *p,DrGsVertex *v,size_t cap,size_t *n,DrBodyPipelineStats *s)
{return build(d,p,v,cap,NULL,0,-1,n,s);}
int dr_body_pipeline_materials(const dr_disc *d,const char *p,DrGsVertex *v,size_t cap,
                              uint64_t *tex0,size_t tcap,size_t *n,DrBodyPipelineStats *s)
{
    if(!tex0 || !tcap) {if(n)*n=0;if(s)memset(s,0,sizeof(*s));return PIPE_FORMAT;}
    return build(d,p,v,cap,tex0,tcap,-1,n,s);
}
int dr_body_pipeline_face(const dr_disc *d,const char *p,DrGsVertex *v,size_t cap,uint64_t *tex0,size_t tcap,
                         int face,size_t *n,DrBodyPipelineStats *s)
{
    if(!tex0 || !tcap) {if(n)*n=0;if(s)memset(s,0,sizeof(*s));return PIPE_FORMAT;}
    return build(d,p,v,cap,tex0,tcap,face,n,s);
}
