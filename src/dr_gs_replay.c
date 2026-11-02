#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Descriptor/pipeline encoding: Tenkaichi3DecompAndroid revision
 * 3ca419c7966b11438e2dc3c9a7e1914f102ed545, port/src/gs/gs_draw.{c,h}.
 * Intentionally rejects features absent from dr_xenos; never silently drops them.
 */
#include "dr_gs_replay.h"
#include "dr_targets.h"
#include "dr_gs_batch.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <math.h>
#include <time.h>
static DrGsReplayProfile last_profile;
void dr_gs_replay_profile(DrGsReplayProfile *p){if(p)*p=last_profile;}
static uint64_t replay_us(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000000u+t.tv_nsec/1000u;}
enum { CAPACITY=DR_XENOS_VERTEX_CAPACITY, FRAME_VERTICES=262144, FRAME_DRAWS=16384 };
_Static_assert(sizeof(Vtx)==sizeof(DrGsVertex),"native vertex ABI size");
#define FIELD(field) _Static_assert(offsetof(Vtx,field)==offsetof(DrGsVertex,field),"vertex ABI: " #field)
FIELD(x);FIELD(y);FIELD(z);FIELD(r);FIELD(g);FIELD(b);FIELD(a);FIELD(s);FIELD(t);FIELD(q);
#undef FIELD
_Static_assert(sizeof(Vu0Uniform)==76*4,"VU uniform CPU layout");
static int translate_effect(const DrGsReplay *r,const GsDraw *d,DrXenosCommand *out)
{
    if(!r->saved_targets || d->target<0 || !dr_targets_is_gs((unsigned)d->target) || d->vu || d->wall || d->count)return DR_GS_UNSUPPORTED;
    memset(out,0,sizeof(*out));out->count=6;
    out->draw=(DrXenosDraw){.logical_width=r->logical_width,.logical_height=r->logical_height,
        .depth=1,.gs_material=1,.tcc=1,.gs_blend=1};
    memcpy(out->draw.scissor,d->scissor,sizeof(d->scissor));
    if(d->native==1) {
        out->draw.effect=1;out->draw.write_mask=7;out->draw.preserve_aux=1;
        out->texture=r->white_texture;
    } else {
        if(d->src<0 || d->src>1 || d->pipeline<0 || (uint32_t)d->pipeline>=r->pipeline_count)return DR_XENOS_INVALID;
        uint32_t key=r->pipeline_keys[d->pipeline];
        if(!(key&(1u<<22)) || (key&~(1023u|(15u<<16)|(1u<<22))))return DR_GS_UNSUPPORTED;
        // A coefficient 1+C cannot be represented by this fixed-function
        // blend path. Reject it rather than silently using ONE.
        if(((key>>1)&7)==5 || ((key>>4)&7)==5)return DR_GS_UNSUPPORTED;
        out->draw.effect=2;out->draw.effect_source=(uint32_t)d->src;
        out->draw.effect_depth_max=d->misc[0];out->draw.write_mask=(key>>16)&15;
        out->draw.blend_key=key&1023;out->draw.blend_constant=d->blendc;
        out->texture=r->texture?r->texture(d->tex,r->opaque):0;
    }
    return out->texture?0:DR_GS_TEXTURE;
}
static void effect_quad(DrGsVertex *v,uint32_t w,uint32_t h)
{
    const DrGsVertex q[6]={{0,0,0,128,128,128,128,0,0,1},{w,0,0,128,128,128,128,1,0,1},
        {0,h,0,128,128,128,128,0,1,1},{0,h,0,128,128,128,128,0,1,1},
        {w,0,0,128,128,128,128,1,0,1},{w,h,0,128,128,128,128,1,1,1}};
    memcpy(v,q,sizeof(q));
}
static int translate(const DrGsReplay *r,const GsDraw *d,uint32_t vertex_count,DrXenosCommand *out)
{
    if(d->native==100 || d->native==101) {
        if(!r->saved_targets || r->logical_width<512 || r->logical_height<448 ||
           d->target<0 || !dr_targets_is_gs((unsigned)d->target) || d->vu || d->wall || d->count)
            return DR_GS_UNSUPPORTED;
        memset(out,0,sizeof(*out));out->count=6;out->texture=r->movie_texture[d->native-100];
        out->draw=(DrXenosDraw){.logical_width=r->logical_width,.logical_height=r->logical_height,.depth=1,.gs_material=1,
            .textured=1,.tfx=1,.tcc=1,.gs_blend=1,.write_mask=15,.sampler=6,.scissor={0,0,512,448}};
        return out->texture?0:DR_GS_TEXTURE;
    }
    if(d->native==4 || d->native==5) {
        if(!r->saved_targets || d->target<0 || !dr_targets_is_gs((unsigned)d->target)) return DR_GS_UNSUPPORTED;
        memset(out,0,sizeof(*out));return 0;
    }
    if(d->native==1 || d->native==3)return translate_effect(r,d,out);
    if(d->vu<0 || d->vu>3) return DR_GS_UNSUPPORTED;
    uint32_t available=d->vu?r->vu_index_count:vertex_count;
    if(d->vu && (!r->vu_vertices || !r->vu_indices || !r->vu_uniforms || r->vu_vertex_count>MAX_VU_VERTS ||
       r->vu_index_count>MAX_VU_IDX || r->vu_uniform_count>MAX_VU_UNIFORMS || d->uniform<0 ||
       (uint32_t)d->uniform>=r->vu_uniform_count)) return DR_XENOS_INVALID;
    if(d->pipeline<0 || (uint32_t)d->pipeline>=r->pipeline_count ||
       d->first>available || !d->count || d->count>available-d->first) return DR_XENOS_INVALID;
    const uint32_t k=r->pipeline_keys[d->pipeline],blend=k&1023u;
    const uint32_t known=1023u|(3u<<10)|(1u<<12)|(3u<<13)|(15u<<16)|(3u<<20)|(1u<<24)|(1u<<23)|(1u<<25);
    if((k&~known) || ((k>>20)&3)!=(uint32_t)d->vu ||
       d->native || d->wall || d->target<0 || (!r->saved_targets && d->target!=r->target) ||
       d->sampler<0 || d->sampler>15 || d->mode[0]<0 || d->mode[0]>2 || ((d->mode[0]==2)!=(d->tex_is_target!=0)) ||
       d->mode[1]<0 || d->mode[1]>3 || d->mode[2]<0 || d->mode[2]>1 ||
       d->mode[3]<0 || d->mode[3]>8 || (d->mode[0]!=0 && d->misc[3]!=0 && !(d->mode[0]==1 && (d->misc[3]==-1 || d->misc[3]==1) && !d->vu && !d->tex_is_target)) ||
       ((blend>>1)&7)>5 || ((blend>>4)&7)>5 || ((blend>>7)&3)>2 || ((k>>13)&3)>2) return DR_GS_UNSUPPORTED;
    if((k&(1u<<24)) && (!r->saved_targets || d->vu!=1 || !dr_targets_is_gs((unsigned)d->target))) return DR_GS_UNSUPPORTED;
    if(!isfinite(d->misc[3]))return DR_XENOS_INVALID;
    if(!isfinite(d->misc[0]) || d->misc[0]<0 || d->misc[0]>255 || floorf(d->misc[0])!=d->misc[0]) return DR_XENOS_INVALID;
    if(!isfinite(d->misc[1]) || d->misc[1]<0 || d->misc[1]>2 || floorf(d->misc[1])!=d->misc[1] ||
       !isfinite(d->misc[2]) || d->misc[2]<0 || d->misc[2]>1 || floorf(d->misc[2])!=d->misc[2]) return DR_XENOS_INVALID;
    if((d->misc[1]!=0 || d->misc[2]!=0 || d->tex_is_target) && (!r->saved_targets || !dr_targets_is_gs((unsigned)d->target))) return DR_GS_UNSUPPORTED;
    out->draw=(DrXenosDraw){.logical_width=r->logical_width,.logical_height=r->logical_height,
        .topology=(k>>13)&3,.blend=blend==69?1:0,.depth=(k>>10)&3,.depth_write=(k>>12)&1};
    out->draw.stencil=(k>>24)&1;
    out->draw.vu_program=(uint32_t)d->vu;
    out->draw.vu_uniform=d->vu?(const float *)&r->vu_uniforms[d->uniform]:NULL;
    out->draw.depth_quantize=(k&(1u<<23))?0:r->depth_max;
    if(d->vu && !(k&(1u<<23)))out->draw.depth_quantize=r->vu_uniforms[d->uniform].misc[2]>0?r->vu_uniforms[d->uniform].misc[2]:0;
    out->draw.sprite_clamp=d->mode[0]==1 && d->misc[3]==-1;
    out->draw.sampler=(uint32_t)d->sampler;out->draw.gs_material=1;out->draw.textured=(uint32_t)d->mode[0];
    out->draw.tfx=(uint32_t)d->mode[1];out->draw.tcc=(uint32_t)d->mode[2];
    out->draw.alpha_test=(uint32_t)d->mode[3];out->draw.alpha_reference=(uint32_t)d->misc[0];
    out->draw.gs_blend=1;out->draw.blend_key=blend;
    out->draw.write_mask=(k>>16)&15;out->draw.blend_constant=d->blendc;
    out->draw.date=(uint32_t)d->misc[1];out->draw.fba=(uint32_t)d->misc[2];
    memcpy(out->draw.texture_rect,d->rect,sizeof(d->rect));
    memcpy(out->draw.scissor,d->scissor,sizeof(d->scissor));
    out->count=d->count;
    /* An untextured draw must ignore a stale texture handle in the descriptor. */
    out->texture=d->mode[0]==0?r->white_texture:(r->texture?r->texture(d->tex,r->opaque):0);
    /* Positive sprite grid is harmless only for the constant white texture.
     * Real textured pixel-grid sampling still requires an explicit implementation. */
    if(d->mode[0]!=0 && d->misc[3]==1 && out->texture!=r->white_texture)return DR_GS_UNSUPPORTED;
    return out->texture?DR_XENOS_OK:DR_GS_TEXTURE;
}
int dr_gs_replay(const DrGsReplay *r,const GsDraw *draws,uint32_t n,
                 const Vtx *vertices,uint32_t vertex_count,uint32_t *failed)
{
    uint64_t started=replay_us();last_profile=(DrGsReplayProfile){.source_draws=n};
    if(failed) *failed=UINT32_MAX;
    if(!r || !draws || (!vertices && vertex_count) || !n || !r->pipeline_keys || !r->pipeline_count || r->saved_targets>1 ||
       (!r->saved_targets && (r->target<0 || !r->physical_width || !r->physical_height ||
       r->physical_width>1280 || r->physical_height>720 || (r->physical_width&31))) ||
       !r->logical_width || !r->logical_height ||
       r->logical_width>4096 || r->logical_height>4096) return DR_XENOS_INVALID;
    uint32_t capacity=r->saved_targets?FRAME_VERTICES:CAPACITY;
    if(n>(r->saved_targets?FRAME_DRAWS:CAPACITY)) return DR_XENOS_FULL;
    DrXenosCommand *commands=calloc(n,sizeof(*commands));
    DrXenosTargetCommand *targets=r->saved_targets?calloc(n*2,sizeof(*targets)):NULL;
    DrGsVertex *copy=malloc(capacity*sizeof(*copy));
    float *vu=malloc((size_t)capacity*48);
    if(!commands || !copy || !vu || (r->saved_targets && !targets)) {
        free(vu);free(targets);free(commands);free(copy);return DR_GS_MEMORY;
    }
    uint32_t total=0,ntargets=0;int error=0;
    for(uint32_t i=0;i<n;i++) {
        error=translate(r,&draws[i],vertex_count,&commands[i]);
        if(!error && commands[i].count>capacity-total) error=DR_XENOS_FULL;
        if(error) {if(failed) *failed=i;break;}
        if(draws[i].native==4 || draws[i].native==5) {
            targets[ntargets].target=(unsigned)draws[i].target;targets[ntargets++].snapshot=draws[i].native==5?1:2;
            continue;
        }
        if(draws[i].native==1 || draws[i].native==3 || draws[i].native==100 || draws[i].native==101) {
            commands[i].vertices=copy+total;effect_quad(copy+total,draws[i].native>=100?512:r->logical_width,draws[i].native>=100?448:r->logical_height);
        } else if(draws[i].vu) {
            commands[i].vu_vertices=vu+total*12;
            for(uint32_t j=0;j<commands[i].count;j++) {
                uint32_t index=r->vu_indices[draws[i].first+j];
                if(index>=r->vu_vertex_count) {error=DR_XENOS_INVALID;if(failed)*failed=i;break;}
                memcpy(vu+(total+j)*12,r->vu_vertices+index*12,48);
            }
            if(error)break;
        } else {
            commands[i].vertices=copy+total;
            memcpy(copy+total,vertices+draws[i].first,commands[i].count*sizeof(*copy));
        }
        total+=commands[i].count;
        if(targets) {
            int start=commands[i].draw.stencil && (i==0 || draws[i-1].native ||
                draws[i-1].target!=draws[i].target || !commands[i-1].draw.stencil);
            if(start) targets[ntargets++]=(DrXenosTargetCommand){.target=(unsigned)draws[i].target,
                .snapshot=3,.command={.texture=r->white_texture}};
            DrXenosTargetCommand next={.target=(unsigned)draws[i].target,.command=commands[i]};
            if(ntargets && dr_gs_batch_merge(&targets[ntargets-1],&next))last_profile.merged_draws++;
            else targets[ntargets++]=next;
        }
    }
    last_profile.replay_commands=targets?ntargets:n;
    last_profile.build_us=replay_us()-started;last_profile.vertices=total;started=replay_us();
    if(!error && targets) error=dr_xenos_replay_targets(targets,ntargets);
    else if(!error) {
        error=dr_xenos_begin(r->physical_width,r->physical_height);
        if(!error) {
            error=dr_xenos_draw_list(commands,n);
            int end=dr_xenos_end();
            if(!error) error=end;
        }
    }
    last_profile.submit_us=replay_us()-started;
    free(vu);free(targets);free(copy);free(commands);return error;
}
