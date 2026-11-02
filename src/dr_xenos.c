/* SPDX-License-Identifier: AGPL-3.0-or-later
 * xsdk calls and GPU memory lifetime follow xsdk/samples/cube/main.c (MIT, Mena).
 * No raw PM4/undocumented registers; shader containers compiled by open xost/xsc.
 */
#include "dr_xenos.h"
#ifdef DR_XBOX360
#include <xsdk/log.h>
#endif
#include "dr_targets.h"
#include <math.h>
#include <stddef.h>
#include <string.h>
#include "xsdk/gpu.h"
#include "xsdk/shader.h"
#include "xsdk/texture.h"
#include "dr_gs_shaders.h"
#include "dr_vu_shaders.h"
#include "dr_effects_shaders.h"
#include <time.h>
enum { MAX_TEXTURES=256,MAX_VERTICES=DR_XENOS_VERTEX_CAPACITY,ARENA_VERTICES=DR_XENOS_FRAME_VERTICES,VERTEX_WORDS=12 };
static DrXenosProfile profile;
static uint64_t xenos_now_us(void)
{
    struct timespec t;return clock_gettime(CLOCK_MONOTONIC,&t)?0:(uint64_t)t.tv_sec*1000000u+t.tv_nsec/1000u;
}
void dr_xenos_profile(DrXenosProfile *p){if(p)*p=profile;}
static struct {
    int opened,active,targets,saved_target;uint32_t used,textures,fence,width,height,target_id;
    XsdkShader vertex,pixel,target_pixel,vu[3],effects[2];
    XsdkTexture texture[MAX_TEXTURES];
    uint32_t live[MAX_TEXTURES],ready[MAX_TEXTURES];
    XsdkSamplerState sampler[16];
    volatile uint32_t *vertices;
} backend;
static uint32_t wait_timeout_ms=1000;
int dr_xenos_set_wait_timeout(uint32_t milliseconds)
{
    if(backend.opened || backend.vertices)return DR_XENOS_STATE;
    int e=dr_targets_set_wait_timeout(milliseconds);if(e)return e;
    wait_timeout_ms=milliseconds;return 0;
}
/* Literal pools are immutable; restore passes and each recording rollover
 * invalidate the cache through the target epoch. Uniforms cache physical VS
 * registers, independently of shader identity; VS literals may overlap uniforms. */
static struct {
    const XsdkBuiltShader *vs,*fs;uint32_t epoch,valid;float constants[76];
    uint32_t gpu_valid,blend_count,pixel_first,pixel_count,texture_valid;
    XsdkDepthStencilState depth;XsdkBlendState blends[2];int scissor[4];float blend_constant[4],pixel[32];
    XsdkTexture textures[16];XsdkSamplerState samplers[16];
} state_cache;
/* Compare values, never borrowed descriptor addresses: snapshots reuse views.
 * Target epoch invalidates every field after restore/resolve/recording rollover. */
static int state_changed(void *saved,const void *value,size_t bytes,uint32_t bit){
    if((state_cache.gpu_valid&bit) && !memcmp(saved,value,bytes)){profile.state_skips++;return 0;}
    memcpy(saved,value,bytes);state_cache.gpu_valid|=bit;profile.state_sets++;return 1;
}
static void pixel_constants(uint32_t first,const float *data,uint32_t count){
    if(count>8){xsdk_gpu_set_pixel_constants(first,data,count);state_cache.gpu_valid&=~16u;return;}
    if(state_cache.pixel_first!=first || state_cache.pixel_count!=count)state_cache.gpu_valid&=~16u;
    if(state_changed(state_cache.pixel,data,count*16,16u))xsdk_gpu_set_pixel_constants(first,data,count);
    state_cache.pixel_first=first;state_cache.pixel_count=count;
}
static void bind_texture(uint32_t slot,const XsdkTexture *texture,const XsdkSamplerState *sampler){
    if(slot>=16){xsdk_gpu_set_texture(slot,texture,sampler);return;}
    uint32_t bit=1u<<slot;
    if((state_cache.texture_valid&bit) && !memcmp(&state_cache.textures[slot],texture,sizeof(*texture)) && !memcmp(&state_cache.samplers[slot],sampler,sizeof(*sampler))){profile.state_skips++;return;}
    state_cache.textures[slot]=*texture;state_cache.samplers[slot]=*sampler;state_cache.texture_valid|=bit;profile.state_sets++;
    xsdk_gpu_set_texture(slot,texture,sampler);
}
static void uniform_constants(uint32_t first,const float *data,uint32_t count){
    if(first+count>19){xsdk_gpu_set_vertex_constants(first,data,count);return;}
    unsigned at=0;
    while(at<count){
        unsigned run=0,start=at;
        while(at<count){
            unsigned n=4-((first+at)&3);if(n>count-at)n=count-at;
            uint32_t mask=((1u<<n)-1)<<(first+at);
            int changed=(state_cache.valid&mask)!=mask || memcmp(state_cache.constants+(first+at)*4,data+at*4,n*16);
            if(!changed){if(run)break;profile.constant_skips+=n;at+=n;start=at;continue;}
            memcpy(state_cache.constants+(first+at)*4,data+at*4,n*16);state_cache.valid|=mask;run+=n;at+=n;
        }
        if(run){xsdk_gpu_set_vertex_constants(first+start,data+start*4,run);profile.constant_registers+=run;}
    }
}
_Static_assert(sizeof(DrGsVertex)==28,"upstream Vtx layout");
static int wait_previous(void)
{
    if(backend.fence){profile.fence_waits++;NTSTATUS status=xsdk_gpu_wait(backend.fence,wait_timeout_ms);if(!NT_SUCCESS(status)){
#ifdef DR_XBOX360
        xsdk_log_write("GS GPU failure operation=vertex-wait status=%08x detail=%u",(unsigned)status,backend.fence);
#endif
        return DR_XENOS_GPU;
    }}
    backend.fence=0;return DR_XENOS_OK;
}
int dr_xenos_sync(void)
{return !backend.opened || backend.active?DR_XENOS_STATE:wait_previous();}
int dr_xenos_close(void)
{
    if(backend.active) return DR_XENOS_STATE;
    int error=wait_previous();if(error) return error; /* no freeing in-flight buffers on timeout */
    if(backend.targets) {error=dr_targets_close();if(error) return error;}
    for(uint32_t i=0;i<backend.textures;i++) if(backend.live[i]) xsdk_texture_free(&backend.texture[i]);
    if(backend.vertices) xsdk_gpu_free(backend.vertices);
    for(unsigned i=0;i<2;i++)xsdk_shader_free(&backend.effects[i]);
    for(unsigned i=0;i<3;i++) xsdk_shader_free(&backend.vu[i]);
    xsdk_shader_free(&backend.target_pixel);xsdk_shader_free(&backend.pixel);xsdk_shader_free(&backend.vertex);
    memset(&backend,0,sizeof(backend));return DR_XENOS_OK;
}
int dr_xenos_open(void)
{
    if(backend.opened || backend.active || backend.vertices) return DR_XENOS_STATE;
    memset(&profile,0,sizeof(profile));
    NTSTATUS status=xsdk_shader_load(&backend.vertex,dr_gs_vert.container,dr_gs_vert.container_bytes);
    if(NT_SUCCESS(status)) status=xsdk_shader_load(&backend.pixel,dr_gs_frag.container,dr_gs_frag.container_bytes);
    if(NT_SUCCESS(status)) status=xsdk_shader_load(&backend.target_pixel,dr_gs_target_frag.container,dr_gs_target_frag.container_bytes);
    const XsdkVertexElement layout[]={
        {.usage=XSDK_USAGE_POSITION,.format=XSDK_VERTEX_FLOAT3,.offset_bytes=0},
        {.usage=XSDK_USAGE_COLOR,.format=XSDK_VERTEX_FLOAT4,.offset_bytes=12},
        {.usage=XSDK_USAGE_TEXCOORD,.format=XSDK_VERTEX_FLOAT3,.offset_bytes=28}
    };
    if(NT_SUCCESS(status)) status=xsdk_shader_set_vertex_layout(&backend.vertex,layout,3,(const uint32_t[]){VERTEX_WORDS*4});
    const XsdkBuiltShader *vu[]={&dr_vu0_vert,&dr_vu4_vert,&dr_vu6_vert};
    const XsdkVertexElement vu_layout[]={
        {.usage=XSDK_USAGE_POSITION,.format=XSDK_VERTEX_FLOAT4,.offset_bytes=0},
        {.usage=XSDK_USAGE_COLOR,.format=XSDK_VERTEX_FLOAT4,.offset_bytes=16},
        {.usage=XSDK_USAGE_TEXCOORD,.format=XSDK_VERTEX_FLOAT4,.offset_bytes=32}};
    for(unsigned i=0;i<3 && NT_SUCCESS(status);i++) {
        status=xsdk_shader_load(&backend.vu[i],vu[i]->container,vu[i]->container_bytes);
        if(NT_SUCCESS(status))status=xsdk_shader_set_vertex_layout(&backend.vu[i],vu_layout,3,(const uint32_t[]){48});
    }
    const XsdkBuiltShader *effects[]={&dr_outline_frag,&dr_dclut_frag};
    for(unsigned i=0;i<2 && NT_SUCCESS(status);i++)
        status=xsdk_shader_load(&backend.effects[i],effects[i]->container,effects[i]->container_bytes);
    if(NT_SUCCESS(status)) backend.vertices=xsdk_gpu_alloc(ARENA_VERTICES*VERTEX_WORDS*4);
    if(!NT_SUCCESS(status) || !backend.vertices) {dr_xenos_close();return DR_XENOS_GPU;}
    /* Same 16 sampler states as the original gs_gpu.c: filter, U/V edge,
     * and whether replacement mip levels may be sampled. */
    for(unsigned i=0;i<16;i++) {
        const XsdkSampler sampler={.mag_filter=(i&1)?XSDK_FILTER_LINEAR:XSDK_FILTER_POINT,
            .min_filter=(i&1)?XSDK_FILTER_LINEAR:XSDK_FILTER_POINT,
            .mip_filter=XSDK_FILTER_LINEAR,.address_u=(i&2)?XSDK_ADDRESS_CLAMP:XSDK_ADDRESS_WRAP,
            .address_v=(i&4)?XSDK_ADDRESS_CLAMP:XSDK_ADDRESS_WRAP,.address_w=XSDK_ADDRESS_CLAMP,
            .level_count=(i&8)?0:1};
        xsdk_texture_sampler_state(&backend.sampler[i],&sampler);
    }
    backend.opened=1;return DR_XENOS_OK;
}
int dr_xenos_texture_create(uint32_t width,uint32_t height,const void *rgba,uint32_t *handle)
{
    const DrXenosMip mip={rgba,width<=1024 && height<=1024?width*height*4:0};
    return dr_xenos_texture_create_mips(width,height,&mip,1,handle);
}
uint32_t dr_xenos_texture_slots(void)
{
    if(!backend.opened) return 0;
    uint32_t available=0;for(unsigned i=0;i<MAX_TEXTURES;i++) available+=!backend.live[i];return available;
}
int dr_xenos_texture_allocate(uint32_t width,uint32_t height,uint32_t levels,uint32_t *handle)
{
    if(handle) *handle=0;
    if(!backend.opened || backend.active) return DR_XENOS_STATE;
    if(!handle || !width || !height || width>1024 || height>1024 || !levels || levels>11) return DR_XENOS_INVALID;
    uint32_t w=width,h=height;
    for(unsigned i=1;i<levels;i++) {if(w==1 && h==1) return DR_XENOS_INVALID;w=w>1?w/2:1;h=h>1?h/2:1;}
    unsigned slot=0;while(slot<MAX_TEXTURES && backend.live[slot]) slot++;
    if(slot==MAX_TEXTURES) return DR_XENOS_FULL;
    int error=wait_previous();if(error) return error;
    if(!NT_SUCCESS(xsdk_texture_create(&backend.texture[slot],XSDK_FORMAT_R8G8B8A8,width,height,levels,true))) return DR_XENOS_GPU;
    backend.live[slot]=1;backend.ready[slot]=0;
    if(backend.textures<=slot) backend.textures=slot+1;
    *handle=slot+1;return 0;
}
int dr_xenos_texture_upload(uint32_t handle,uint32_t level,const void *rgba,uint32_t bytes)
{
    if(!backend.opened || backend.active) return DR_XENOS_STATE;
    if(!handle || handle>backend.textures || !backend.live[handle-1] || !rgba) return DR_XENOS_INVALID;
    XsdkTexture *t=&backend.texture[handle-1];
    if(level>=t->levels) return DR_XENOS_INVALID;
    uint32_t w=t->width>>level,h=t->height>>level;w=w?w:1;h=h?h:1;
    if(bytes!=w*h*4) return DR_XENOS_INVALID;
    int error=wait_previous();if(error) return error;
    xsdk_texture_upload(t,level,rgba,w*4);xsdk_gpu_invalidate(t->address,t->size_bytes);
    backend.ready[handle-1]|=1u<<level;return 0;
}
int dr_xenos_texture_destroy(uint32_t handle)
{
    if(!backend.opened || backend.active) return DR_XENOS_STATE;
    if(!handle || handle>backend.textures || !backend.live[handle-1]) return DR_XENOS_INVALID;
    int error=wait_previous();if(error) return error;
    xsdk_texture_free(&backend.texture[handle-1]);backend.live[handle-1]=backend.ready[handle-1]=0;return 0;
}
int dr_xenos_texture_create_mips(uint32_t width,uint32_t height,const DrXenosMip *mips,uint32_t levels,uint32_t *handle)
{
    if(handle) *handle=0;
    if(!backend.opened || backend.active) return DR_XENOS_STATE;
    if(!width || !height || width>1024 || height>1024 || !mips || !handle || !levels || levels>11) return DR_XENOS_INVALID;
    uint32_t w=width,h=height;
    for(uint32_t i=0;i<levels;i++) {
        if(!mips[i].pixels || mips[i].bytes!=w*h*4 || (i+1<levels && w==1 && h==1)) return DR_XENOS_INVALID;
        w=w>1?w/2:1;h=h>1?h/2:1;
    }
    uint32_t native;int error=dr_xenos_texture_allocate(width,height,levels,&native);if(error) return error;
    XsdkTexture *texture=&backend.texture[native-1];
    w=width;
    for(uint32_t i=0;i<levels;i++) {xsdk_texture_upload(texture,i,mips[i].pixels,w*4);w=w>1?w/2:1;}
    xsdk_gpu_invalidate(texture->address,texture->size_bytes);
    backend.ready[native-1]=(1u<<levels)-1;*handle=native;return DR_XENOS_OK;
}
int dr_xenos_begin(uint32_t width,uint32_t height)
{
    if(!backend.opened || backend.active) return DR_XENOS_STATE;
    if(!width || !height || width>1280 || height>720 || (width&31)) return DR_XENOS_INVALID;
    int error=wait_previous();if(error) return error;
    memset(&state_cache,0,sizeof(state_cache));backend.width=width;backend.height=height;backend.used=0;backend.active=1;
    xsdk_gpu_set_target(width,height);
    return DR_XENOS_OK;
}
int dr_xenos_target_create(unsigned id,uint32_t width,uint32_t height)
{
    if(!backend.opened || backend.active) return DR_XENOS_STATE;
    int error=wait_previous();if(error) return error;
    if(!backend.targets) {
        error=dr_targets_open();if(error) return error;
        backend.targets=1;
    }
    return dr_targets_ensure(id,width,height);
}
int dr_xenos_target_create_gs(unsigned id,uint32_t width,uint32_t height)
{
    if(!backend.opened || backend.active) return DR_XENOS_STATE;
    int error=wait_previous();if(error) return error;
    if(!backend.targets) {error=dr_targets_open();if(error) return error;backend.targets=1;}
    return dr_targets_ensure_gs(id,width,height);
}
uint32_t dr_xenos_target_texture(unsigned id)
{return dr_targets_is_gs(id)?0x80000000u|id:0;}
int dr_xenos_snapshot(unsigned kind)
{
    if(!backend.active || !backend.saved_target || !dr_targets_is_gs(backend.target_id)) return DR_XENOS_STATE;
    uint64_t started=xenos_now_us();profile.target_snapshots++;
    int error=dr_targets_snapshot(kind);
    profile.select_us+=xenos_now_us()-started;return error;
}
int dr_xenos_target_drop(unsigned id)
{
    if(!backend.opened || backend.active || !backend.targets) return DR_XENOS_STATE;
    int error=wait_previous();return error?error:dr_targets_drop(id);
}
int dr_xenos_begin_target(unsigned id)
{
    if(!backend.opened || backend.active || !backend.targets) return DR_XENOS_STATE;
    uint32_t w,h;int error=dr_targets_size(id,&w,&h);if(error) return error;
    error=wait_previous();if(error) return error;
    error=dr_targets_bind(id);if(error) return error;
    memset(&state_cache,0,sizeof(state_cache));backend.width=w;backend.height=h;backend.used=0;backend.active=1;backend.saved_target=1;backend.target_id=id;return 0;
}
int dr_xenos_select_target(unsigned id)
{
    if(!backend.active || !backend.saved_target) return DR_XENOS_STATE;
    uint64_t started=xenos_now_us();
    uint32_t w,h;int error=dr_targets_size(id,&w,&h);if(error) return error;
    error=dr_targets_bind(id);if(error) return error;
    backend.width=w;backend.height=h;backend.target_id=id;
    profile.target_selects++;profile.select_us+=xenos_now_us()-started;return 0;
}
static int validate_draw(const void *data,uint32_t count,uint32_t texture,const DrXenosDraw *draw,
                         uint32_t width,uint32_t height)
{
    profile.validated_vertices+=count;
    int target_texture=(texture&0x80000000u)!=0;
    if(!data || !draw || !count || !texture ||
       (target_texture?!dr_targets_is_gs(texture&0x7fffffffu):texture>backend.textures || !backend.live[texture-1] ||
        backend.ready[texture-1]!=(1u<<backend.texture[texture-1].levels)-1) || !draw->logical_width || !draw->logical_height ||
       draw->logical_width>4096 || draw->logical_height>4096 || draw->topology>2 || draw->blend>2 || draw->depth>3 || draw->depth_write>1 ||
       draw->scissor[0]<0 || draw->scissor[1]<0 || draw->scissor[2]<=0 || draw->scissor[3]<=0 ||
       (uint32_t)draw->scissor[0]>=width || (uint32_t)draw->scissor[1]>=height ||
       (uint32_t)draw->scissor[2]>width-(uint32_t)draw->scissor[0] ||
       (uint32_t)draw->scissor[3]>height-(uint32_t)draw->scissor[1] ||
       (draw->topology==0 && count%3) || (draw->topology==1 && count%2)) return DR_XENOS_INVALID;
    if(draw->effect>2 || (draw->effect && (draw->vu_program || draw->date || draw->fba || draw->stencil ||
       draw->depth!=1 || draw->depth_write || draw->topology || !draw->gs_material || !draw->gs_blend)))return DR_XENOS_INVALID;
    if(draw->effect==1 && (draw->write_mask!=7 || !draw->preserve_aux))return DR_XENOS_INVALID;
    if(draw->effect==2 && (draw->effect_source>1 || texture>backend.textures || !texture ||
       backend.texture[texture-1].width!=256 || backend.texture[texture-1].height!=1 ||
       (draw->effect_depth_max!=65535.0f && draw->effect_depth_max!=16777215.0f &&
        draw->effect_depth_max!=4294967295.0f)))return DR_XENOS_INVALID;
    if(draw->vu_program>3 || !isfinite(draw->depth_quantize) ||
       (draw->depth_quantize!=0 && draw->depth_quantize!=65535.0f &&
        draw->depth_quantize!=16777215.0f && draw->depth_quantize!=4294967295.0f)) return DR_XENOS_INVALID;
    /* Defensive: prevent effect-storm crash (shadow/outline effects accumulating). */
    if((draw->effect==1 || draw->effect==2) && backend.active && backend.used>ARENA_VERTICES-256) return DR_XENOS_FULL;
    if(draw->vu_program) {
        if(!draw->vu_uniform || ((uintptr_t)draw->vu_uniform&3) || ((uintptr_t)data&3) || draw->logical_width!=1024 || draw->logical_height!=1024 || draw->topology) return DR_XENOS_INVALID;
        for(unsigned i=0;i<76;i++) if(!isfinite(draw->vu_uniform[i])) return DR_XENOS_INVALID;
        if(draw->vu_uniform[70]==0) return DR_XENOS_INVALID;
        if(draw->vu_program==1 && (draw->vu_uniform[71]<0 || draw->vu_uniform[71]>5 || floorf(draw->vu_uniform[71])!=draw->vu_uniform[71])) return DR_XENOS_INVALID;
    }
    if(draw->sampler>15 || draw->gs_material>1 || draw->gs_blend>1 || draw->date>2 || draw->fba>1 || draw->stencil>2 || draw->preserve_aux>1 ||
       (draw->gs_material && (draw->textured>2 || draw->tfx>3 || draw->tcc>1 || draw->alpha_test>8 || draw->alpha_reference>255)) ||
       (draw->gs_blend && (draw->blend_key>1023 || ((draw->blend_key>>1)&7)>5 ||
        ((draw->blend_key>>4)&7)>5 || ((draw->blend_key>>7)&3)>2 || draw->write_mask>15 ||
        !isfinite(draw->blend_constant) || draw->blend_constant<0 || draw->blend_constant>2))) return DR_XENOS_INVALID;
    if(draw->sprite_clamp>1 || (draw->sprite_clamp && (draw->textured!=1 || draw->vu_program)))return DR_XENOS_INVALID;
    if(draw->gs_material && (draw->textured==2 || draw->sprite_clamp)) {
        /* Ordinary sprites may span repeated tiles outside [0,1], as in the
         * original gs.frag clamp-before-sampler path. Target crops stay local. */
        for(unsigned i=0;i<4;i++) if(!isfinite(draw->texture_rect[i]) ||
            (draw->textured==2?(draw->texture_rect[i]<0 || draw->texture_rect[i]>1):fabsf(draw->texture_rect[i])>4096))return DR_XENOS_INVALID;
        if(draw->texture_rect[0]>draw->texture_rect[2] || draw->texture_rect[1]>draw->texture_rect[3]) return DR_XENOS_INVALID;
    }
    if(target_texture && (!draw->gs_material || draw->textured!=2)) return DR_XENOS_INVALID;
    if(draw->gs_material && draw->textured==2 && !target_texture) return DR_XENOS_INVALID;
    if(count>MAX_VERTICES) return DR_XENOS_FULL;
    if(draw->vu_program) {
        const float *vertices=data;
        for(uint32_t i=0;i<count*12;i++)if(!isfinite(vertices[i]))return DR_XENOS_INVALID;
        return 0;
    }
    const DrGsVertex *vertices=data;
    for(uint32_t i=0;i<count;i++) {
        const DrGsVertex *v=&vertices[i];
        if(!isfinite(v->x) || !isfinite(v->y) || !isfinite(v->z) || v->z<0 || v->z>1 ||
           !isfinite(v->s) || !isfinite(v->t) || !isfinite(v->q) || v->q==0) return DR_XENOS_INVALID;
        if(draw->gs_material && draw->textured==2 &&
           (v->q<=0)) return DR_XENOS_INVALID;
    }
    return DR_XENOS_OK;
}
static int needs_effect_snapshot(const DrXenosDraw *d)
{return d->effect==1 || (d->effect==2 && d->effect_source==1);}
static int validate_features(const DrXenosDraw *draw,uint32_t texture,unsigned id,int saved,uint32_t w,uint32_t h,int pending_date,int pending_effect)
{
    int gs=saved && dr_targets_is_gs(id);
    if((draw->date || draw->fba || draw->stencil || draw->effect) && !gs) return DR_XENOS_STATE;
    if((texture&0x80000000u) && !gs && !dr_targets_ready(texture&0x7fffffffu)) return DR_XENOS_STATE;
    if(needs_effect_snapshot(draw) && !pending_effect && !dr_targets_snapshot_texture(1,id,w,h))return DR_XENOS_STATE;
    if(draw->date && !pending_date && !dr_targets_snapshot_texture(0,id,w,h)) return DR_XENOS_STATE;
    return 0;
}
int dr_xenos_draw_list(const DrXenosCommand *commands,uint32_t count)
{
    if(!backend.active) return DR_XENOS_STATE;
    if(!commands || !count) return DR_XENOS_INVALID;
    if(count>MAX_VERTICES) return DR_XENOS_FULL;
    uint32_t total=0;
    for(uint32_t i=0;i<count;i++) {
        int error=validate_draw(commands[i].draw.vu_program?(const void *)commands[i].vu_vertices:commands[i].vertices,commands[i].count,commands[i].texture,&commands[i].draw,backend.width,backend.height);
        if(!error) error=validate_features(&commands[i].draw,commands[i].texture,backend.target_id,backend.saved_target,backend.width,backend.height,0,0);
        if(error) return error;
        if(commands[i].count>ARENA_VERTICES-backend.used-total) return DR_XENOS_FULL;
        total+=commands[i].count;
    }
    for(uint32_t i=0;i<count;i++) {
        int error=commands[i].draw.vu_program?dr_xenos_draw_vu(commands[i].vu_vertices,commands[i].count,commands[i].texture,&commands[i].draw):
            dr_xenos_draw(commands[i].vertices,commands[i].count,commands[i].texture,&commands[i].draw);
        if(error) return error;
    }
    return DR_XENOS_OK;
}
static int draw_impl(const void *data,uint32_t count,uint32_t texture,const DrXenosDraw *draw,int prevalidated)
{
    if(!backend.active) return DR_XENOS_STATE;
    if(count>MAX_VERTICES || count>ARENA_VERTICES-backend.used) return DR_XENOS_FULL;
    int error=prevalidated?0:validate_draw(data,count,texture,draw,backend.width,backend.height);
    if(prevalidated)profile.reused_validation_vertices+=count;
    if(error) return error;
    int gs=backend.saved_target && dr_targets_is_gs(backend.target_id);
    error=validate_features(draw,texture,backend.target_id,backend.saved_target,backend.width,backend.height,0,0);if(error) return error;
    const XsdkTexture *date=gs?dr_targets_snapshot_texture(0,backend.target_id,backend.width,backend.height):NULL;
    const XsdkTexture *image=NULL,*target_alpha=NULL;
    const XsdkTexture *effect_depth=NULL,*effect_aux=NULL;
    if(draw->effect==2 && draw->effect_source==0) {
        /* Only depth CLUT consumes live depth. Outline and ID CLUT consume
         * an already frozen alpha snapshot; another resolve changes nothing.
         * Resolve prior writes before any sampled depth/alpha consumer. The
         * frozen ID snapshot is independent of the live auxiliary attachment. */
        error=dr_targets_store();if(error)return error;
        error=dr_targets_bind(backend.target_id);if(error)return error;
        effect_depth=dr_targets_texture(backend.target_id,2);
        effect_aux=dr_targets_snapshot_texture(1,backend.target_id,backend.width,backend.height);
    }
    if(draw->effect){
        effect_depth=dr_targets_texture(backend.target_id,2);
        effect_aux=dr_targets_snapshot_texture(1,backend.target_id,backend.width,backend.height);
    }
    if((texture&0x80000000u) && gs) {
        unsigned source=texture&0x7fffffffu;
        /* An already resolved OTHER target is immutable while this target is
         * active. Sampling it needs no eDRAM save/restore. Self-feedback and
         * first use of an uninitialized source retain the checked resolve path. */
        if(source==backend.target_id || !dr_targets_ready(source)){
            error=dr_targets_store();if(error) return error;
            if(source!=backend.target_id) {
                error=dr_targets_bind(source);if(error) return error;
                error=dr_targets_store();if(error) return error;
            }
            error=dr_targets_bind(backend.target_id);if(error) return error;
        }
        image=dr_targets_texture(source,0);
    } else image=(texture&0x80000000u)?dr_targets_texture(texture&0x7fffffffu,0):&backend.texture[texture-1];
    if(texture&0x80000000u) target_alpha=dr_targets_texture(texture&0x7fffffffu,1);
    /* bind_texture() dereferences its argument. A target view that is not ready must fail this draw with a log line,
     * not fault the CPU (the console returns to the dashboard; Xenia may tolerate a read at address 0). */
    if(!image || ((texture&0x80000000u) && !target_alpha) || (draw->effect==2 && !effect_depth) || (draw->effect==1 && !effect_aux)) {
#ifdef DR_XBOX360
        xsdk_log_write("Xenos NULL view: image=%d target_alpha=%d depth=%d aux=%d effect=%u source=%u texture=%08x target=%u",
            image!=NULL,target_alpha!=NULL,effect_depth!=NULL,effect_aux!=NULL,draw->effect,draw->effect_source,texture,backend.target_id);
#endif
        return DR_XENOS_STATE;
    }
    /* A complete draw emits <16k dwords; preserve this margin before any of
     * its state is written, while all vertices remain immutable in the arena. */
    if(gs){error=dr_targets_ensure_commands(16384);if(error)return error;}
    uint32_t epoch=gs?dr_targets_epoch():0;
    if(!gs || state_cache.epoch!=epoch){memset(&state_cache,0,sizeof(state_cache));state_cache.epoch=epoch;}
    uint32_t first=backend.used*VERTEX_WORDS;
    const DrGsVertex *vertices=data;
    if(draw->vu_program) {
        /* Raw CPU float words; the arena is float storage, so the copy is
         * bit-identical without per-word conversions. */
        memcpy((uint32_t *)backend.vertices+first,data,(size_t)count*VERTEX_WORDS*4);
    } else {
        for(uint32_t i=0;i<count;i++) {
            const DrGsVertex *v=&vertices[i];
            const float values[VERTEX_WORDS]={v->x,v->y,v->z,v->r/128.0f,v->g/128.0f,v->b/128.0f,v->a/128.0f,v->s,v->t,v->q,0,0};
            memcpy((uint32_t *)backend.vertices+first+i*VERTEX_WORDS,values,sizeof(values));
        }
    }
    uint32_t address=xsdk_gpu_address(backend.vertices)+first*4,bytes=count*VERTEX_WORDS*4;
    xsdk_gpu_invalidate(address,bytes);
    const uint32_t comparisons[]={XSDK_COMPARE_NEVER,XSDK_COMPARE_ALWAYS,XSDK_COMPARE_GREATER_EQUAL,XSDK_COMPARE_GREATER};
    const XsdkDepthStencil depth={.depth_test=true,.depth_write=draw->depth_write!=0,.depth_compare=comparisons[draw->depth],
        .stencil_test=draw->stencil!=0,.stencil_compare=draw->stencil==2?XSDK_COMPARE_ALWAYS:XSDK_COMPARE_EQUAL,.stencil_reference=0,
        .stencil_read_mask=255,.stencil_write_mask=255,.stencil_fail=XSDK_STENCIL_KEEP,
        .stencil_depth_fail=XSDK_STENCIL_KEEP,.stencil_pass=draw->stencil==2?XSDK_STENCIL_REPLACE:XSDK_STENCIL_INCREMENT_CLAMP};
    XsdkDepthStencilState depth_state;xsdk_gpu_depth_stencil_state(&depth_state,&depth);if(state_changed(&state_cache.depth,&depth_state,sizeof(depth_state),1u))xsdk_gpu_set_depth_stencil(&depth_state);
    XsdkBlend blend={.color_source=draw->blend?XSDK_BLEND_SRC_ALPHA:XSDK_BLEND_ONE,
        .color_dest=draw->blend==1?XSDK_BLEND_ONE_MINUS_SRC_ALPHA:draw->blend==2?XSDK_BLEND_ONE:XSDK_BLEND_ZERO,
        .color_op=XSDK_BLEND_OP_ADD,.alpha_source=XSDK_BLEND_ONE,.alpha_dest=XSDK_BLEND_ZERO,
        .alpha_op=XSDK_BLEND_OP_ADD,.write_mask=XSDK_WRITE_ALL};
    if(draw->gs_blend) {
        uint32_t b=draw->blend_key,src=(b>>1)&7,dst=(b>>4)&7,c=(b>>7)&3;
        const uint32_t factor[]={XSDK_BLEND_SRC_ALPHA,XSDK_BLEND_DST_ALPHA,XSDK_BLEND_CONSTANT_COLOR};
        const uint32_t inverse[]={XSDK_BLEND_ONE_MINUS_SRC_ALPHA,XSDK_BLEND_ONE_MINUS_DST_ALPHA,XSDK_BLEND_ONE_MINUS_CONSTANT_COLOR};
        const uint32_t factors[]={XSDK_BLEND_ZERO,XSDK_BLEND_ONE,factor[c],factor[c],inverse[c],XSDK_BLEND_ONE};
        blend.color_source=(b&1)?((b&(1u<<9))?XSDK_BLEND_ONE:factors[src]):XSDK_BLEND_ONE;
        blend.color_dest=(b&1)?factors[dst]:XSDK_BLEND_ZERO;
        blend.color_op=!(b&1)?XSDK_BLEND_OP_ADD:src==3?XSDK_BLEND_OP_REVERSE_SUBTRACT:dst==3?XSDK_BLEND_OP_SUBTRACT:XSDK_BLEND_OP_ADD;
        blend.write_mask=draw->write_mask;
        const float constant[]={draw->blend_constant,draw->blend_constant,draw->blend_constant,draw->blend_constant};
        if(state_changed(state_cache.blend_constant,constant,sizeof(constant),8u))xsdk_gpu_set_blend_constants(constant);
    }
    if(draw->effect==1) {
        blend.color_source=XSDK_BLEND_ONE;blend.color_dest=XSDK_BLEND_ONE;
        blend.color_op=XSDK_BLEND_OP_REVERSE_SUBTRACT;blend.write_mask=7;
    }
    XsdkBlendState blend_state;xsdk_gpu_blend_state(&blend_state,&blend);
    if(gs) {
        const XsdkBlend aux={.color_source=XSDK_BLEND_ONE,.color_dest=XSDK_BLEND_ZERO,.color_op=XSDK_BLEND_OP_ADD,
            .alpha_source=XSDK_BLEND_ONE,.alpha_dest=XSDK_BLEND_ZERO,.alpha_op=XSDK_BLEND_OP_ADD,
            .write_mask=(blend.write_mask&XSDK_WRITE_ALPHA) && !draw->preserve_aux?XSDK_WRITE_RED:0};
        XsdkBlendState states[2]={blend_state,{0}};xsdk_gpu_blend_state(&states[1],&aux);if(state_cache.blend_count!=2)state_cache.gpu_valid&=~2u;
        if(state_changed(state_cache.blends,states,sizeof(states),2u))xsdk_gpu_set_blends(states,2);
        state_cache.blend_count=2;
    } else {
        if(state_cache.blend_count!=1)state_cache.gpu_valid&=~2u;
        if(state_changed(state_cache.blends,&blend_state,sizeof(blend_state),2u))xsdk_gpu_set_blend(&blend_state);
        state_cache.blend_count=1;
    }
    if(state_changed(state_cache.scissor,draw->scissor,sizeof(draw->scissor),4u))xsdk_gpu_set_scissor(draw->scissor[0],draw->scissor[1],draw->scissor[2],draw->scissor[3]);
    int sampled_target=draw->gs_material && draw->textured==2;
    xsdk_gpu_set_shaders(draw->vu_program?&backend.vu[draw->vu_program-1]:&backend.vertex,draw->effect?&backend.effects[draw->effect-1]:sampled_target?&backend.target_pixel:&backend.pixel);
    const XsdkBuiltShader *vs[]={&dr_gs_vert,&dr_vu0_vert,&dr_vu4_vert,&dr_vu6_vert};
    if(state_cache.vs!=vs[draw->vu_program]){xsdk_built_shader_set_literals(vs[draw->vu_program]);state_cache.vs=vs[draw->vu_program];for(unsigned k=0;k<state_cache.vs->literal_count;k++){unsigned reg=state_cache.vs->literals[k].reg;if(reg<19)state_cache.valid&=~(1u<<reg);}profile.literal_sets++;}else profile.literal_skips++;
    const XsdkBuiltShader *fs=draw->effect==1?&dr_outline_frag:draw->effect==2?&dr_dclut_frag:sampled_target?&dr_gs_target_frag:&dr_gs_frag;
    if(state_cache.fs!=fs){xsdk_built_shader_set_literals(fs);state_cache.fs=fs;state_cache.gpu_valid&=~16u;profile.literal_sets++;}else profile.literal_skips++;
    const float extent[]={2.0f/draw->logical_width,2.0f/draw->logical_height,0,0};
    if(draw->vu_program) {
        const uint32_t reg[]={0,DR_VU0_VERT_UNIFORM_1_0,DR_VU4_VERT_UNIFORM_1_0,DR_VU6_VERT_UNIFORM_1_0};
        const uint32_t regs[]={0,DR_VU0_VERT_UNIFORM_1_0_REGISTERS,DR_VU4_VERT_UNIFORM_1_0_REGISTERS,DR_VU6_VERT_UNIFORM_1_0_REGISTERS};
        uniform_constants(reg[draw->vu_program],draw->vu_uniform,regs[draw->vu_program]);
    } else uniform_constants(DR_GS_VERT_PUSH,extent,DR_GS_VERT_PUSH_REGISTERS);
    float sprite_rect[4];memcpy(sprite_rect,draw->texture_rect,sizeof(sprite_rect));
    if(draw->sprite_clamp){
        sprite_rect[0]+=0.5f/image->width;sprite_rect[1]+=0.5f/image->height;
        sprite_rect[2]-=0.5f/image->width;sprite_rect[3]-=0.5f/image->height;
    }
    const float material[]={draw->gs_material?(float)draw->textured:1,
        draw->gs_material?(float)draw->tfx:0,draw->gs_material?(float)draw->tcc:1,
        draw->gs_material?(float)draw->alpha_test:0,(float)draw->alpha_reference,
        draw->gs_blend && (draw->blend_key&(1u<<9))?1:0,(float)draw->date,(float)draw->fba,
        1.0f/backend.width,1.0f/backend.height,draw->depth_quantize,(float)draw->sprite_clamp,
        sprite_rect[0],sprite_rect[1],sprite_rect[2],sprite_rect[3]};
    if(draw->effect==1) {
        const float params[]={1.0f/backend.width,1.0f/backend.height,100.0f/255.0f,0,
            0.5f/backend.width,0.5f/backend.height,(backend.width-0.5f)/backend.width,(backend.height-0.5f)/backend.height};
        pixel_constants(DR_OUTLINE_FRAG_PUSH,params,DR_OUTLINE_FRAG_PUSH_REGISTERS);
        bind_texture(DR_OUTLINE_FRAG_SAMPLER_0_0,effect_aux,&backend.sampler[6]);
    } else if(draw->effect==2) {
        const float params[]={(float)draw->effect_source,draw->effect_depth_max,1.0f/backend.width,1.0f/backend.height,
            1.0f/backend.width,1.0f/backend.height,(draw->blend_key&(1u<<9))?1.0f:0.0f,0};
        pixel_constants(DR_DCLUT_FRAG_PUSH,params,DR_DCLUT_FRAG_PUSH_REGISTERS);
        bind_texture(DR_DCLUT_FRAG_SAMPLER_0_0,effect_depth,&backend.sampler[6]);
        bind_texture(DR_DCLUT_FRAG_SAMPLER_0_1,effect_aux?effect_aux:effect_depth,&backend.sampler[6]);
        bind_texture(DR_DCLUT_FRAG_SAMPLER_0_2,image,&backend.sampler[6]);
    } else {
        pixel_constants(DR_GS_FRAG_PUSH,material,DR_GS_FRAG_PUSH_REGISTERS);
        bind_texture(DR_GS_FRAG_SAMPLER_0_0,image,&backend.sampler[draw->sampler]);
        bind_texture(DR_GS_FRAG_SAMPLER_0_1,date?date:image,&backend.sampler[6]);
        if(sampled_target) bind_texture(DR_GS_TARGET_FRAG_SAMPLER_0_2,target_alpha,&backend.sampler[draw->sampler]);
    }
    xsdk_gpu_set_vertices(0,address,bytes);
    const uint32_t primitive[]={XSDK_PRIMITIVE_TRIANGLE_LIST,XSDK_PRIMITIVE_LINE_LIST,XSDK_PRIMITIVE_POINT_LIST};
    if(gs && draw->stencil==1)dr_targets_stencil_written();
    xsdk_gpu_draw(primitive[draw->topology],count);
    backend.used+=count;profile.draws++;return DR_XENOS_OK;
}
int dr_xenos_draw(const DrGsVertex *v,uint32_t count,uint32_t texture,const DrXenosDraw *draw)
{if(!draw || draw->vu_program)return DR_XENOS_INVALID;return draw_impl(v,count,texture,draw,0);}
int dr_xenos_draw_vu(const float *v,uint32_t count,uint32_t texture,const DrXenosDraw *draw)
{if(!draw || !draw->vu_program)return DR_XENOS_INVALID;return draw_impl(v,count,texture,draw,0);}
int dr_xenos_end(void)
{
    if(!backend.active) return DR_XENOS_STATE;
    int error=backend.saved_target?dr_targets_store():0;
    backend.saved_target=0;
    backend.fence=xsdk_gpu_fence();backend.active=0;
    return error;
}
/* Clear only stencil at a shadow-run boundary, preserving both MRTs and Z. */
static DrXenosDraw stencil_clear_draw(uint32_t w,uint32_t h) {
    return (DrXenosDraw){.logical_width=w,.logical_height=h,.depth=1,.stencil=2,
        .gs_material=1,.tcc=1,.gs_blend=1,.write_mask=0,.scissor={0,0,(int)w,(int)h}};
}
static void stencil_clear_vertices(DrGsVertex v[6],uint32_t w,uint32_t h) {
    const DrGsVertex quad[6]={{0,0,0,128,128,128,128,0,0,1},{w,0,0,128,128,128,128,0,0,1},
        {0,h,0,128,128,128,128,0,0,1},{0,h,0,128,128,128,128,0,0,1},
        {w,0,0,128,128,128,128,0,0,1},{w,h,0,128,128,128,128,0,0,1}};
    memcpy(v,quad,sizeof(quad));
}
/* Preflight the complete bounded frame before submitting any batch. GPU
 * draw calls stay bounded to 1024 vertices; the immutable frame arena covers
 * the already checked 262144-vertex limit without eDRAM spills; source-target feedback cannot be split. */
int dr_xenos_replay_targets(const DrXenosTargetCommand *commands,uint32_t count)
{
    if(!backend.opened || backend.active || !backend.targets)return DR_XENOS_STATE;
    if(!commands || !count)return DR_XENOS_INVALID;
    if(count>32768)return DR_XENOS_FULL;
    uint32_t total=0;int date_target=-1,effect_target=-1;
    uint64_t preflight_started=xenos_now_us();
    for(uint32_t i=0;i<count;i++){
        uint32_t w,h;int error=dr_targets_size(commands[i].target,&w,&h);if(error)return error;
        const DrXenosCommand *c=&commands[i].command;
        if(commands[i].snapshot){
            if(commands[i].snapshot>3 || c->count || !dr_targets_is_gs(commands[i].target))return DR_XENOS_INVALID;
            if(commands[i].snapshot==3){
                DrGsVertex quad[6];stencil_clear_vertices(quad,w,h);DrXenosDraw clear=stencil_clear_draw(w,h);
                error=validate_draw(quad,6,c->texture,&clear,w,h);if(error)return error;
                if(total>262144-6)return DR_XENOS_FULL;
                total+=6;
            }
            if(commands[i].snapshot==2)effect_target=(int)commands[i].target;
            if(commands[i].snapshot==1)date_target=(int)commands[i].target;
            continue;
        }
        if(!c->count)return DR_XENOS_INVALID;
        if(c->count>262144-total)return DR_XENOS_FULL;
        total+=c->count;
        if(c->count>MAX_VERTICES && c->draw.textured==2)return DR_XENOS_FULL;
        uint32_t unit=c->draw.topology==0?3:c->draw.topology==1?2:1,limit=MAX_VERTICES/unit*unit;
        for(uint32_t at=0;at<c->count;){
            uint32_t n=c->count-at;if(n>limit)n=limit;
            const void *data=c->draw.vu_program?(const void*)(c->vu_vertices?c->vu_vertices+at*12:NULL):(const void*)(c->vertices?c->vertices+at:NULL);
            error=validate_draw(data,n,c->texture,&c->draw,w,h);
            if(error){
#ifdef DR_XBOX360
                xsdk_log_write("Xenos preflight error=%d command=%u at=%u count=%u texture=%08x target=%u size=%u,%u topology=%u vu=%u scissor=%d,%d,%d,%d",error,i,at,n,c->texture,commands[i].target,w,h,c->draw.topology,c->draw.vu_program,c->draw.scissor[0],c->draw.scissor[1],c->draw.scissor[2],c->draw.scissor[3]);
                xsdk_log_write("Xenos rect %.9g %.9g %.9g %.9g sampler=%u",(double)c->draw.texture_rect[0],(double)c->draw.texture_rect[1],(double)c->draw.texture_rect[2],(double)c->draw.texture_rect[3],c->draw.sampler);
                const DrXenosDraw *d=&c->draw;
                xsdk_log_write("Xenos material logical=%u,%u effect=%u source=%u max=%.9g tex=%u tfx=%u tcc=%u alpha=%u ref=%u blend=%u key=%u mask=%u const=%.9g depth=%u write=%u date=%u fba=%u stencil=%u preserve=%u clamp=%u",d->logical_width,d->logical_height,d->effect,d->effect_source,(double)d->effect_depth_max,d->textured,d->tfx,d->tcc,d->alpha_test,d->alpha_reference,d->gs_blend,d->blend_key,d->write_mask,(double)d->blend_constant,d->depth,d->depth_write,d->date,d->fba,d->stencil,d->preserve_aux,d->sprite_clamp);
                if(c->texture && c->texture<=backend.textures)xsdk_log_write("Xenos texture shape=%u,%u levels=%u ready=%u live=%u",backend.texture[c->texture-1].width,backend.texture[c->texture-1].height,backend.texture[c->texture-1].levels,backend.ready[c->texture-1],backend.live[c->texture-1]);
                if(!c->draw.vu_program && data){const DrGsVertex *v=data;for(unsigned j=0;j<n;j++)if(!isfinite(v[j].x)||!isfinite(v[j].y)||!isfinite(v[j].z)||v[j].z<0||v[j].z>1||!isfinite(v[j].s)||!isfinite(v[j].t)||!isfinite(v[j].q)||v[j].q==0){xsdk_log_write("Xenos invalid vertex=%u xyz=%.8g,%.8g,%.8g stq=%.8g,%.8g,%.8g",j,(double)v[j].x,(double)v[j].y,(double)v[j].z,(double)v[j].s,(double)v[j].t,(double)v[j].q);break;}}
#endif
                return error;
            }
            at+=n;
        }
        if(c->draw.date && date_target>=0 && date_target!=(int)commands[i].target)return DR_XENOS_STATE;
        if(needs_effect_snapshot(&c->draw) && effect_target>=0 && effect_target!=(int)commands[i].target)return DR_XENOS_STATE;
        error=validate_features(&c->draw,c->texture,commands[i].target,1,w,h,date_target==(int)commands[i].target,effect_target==(int)commands[i].target);if(error)return error;
    }
    profile.preflight_us+=xenos_now_us()-preflight_started;
    int error=dr_xenos_begin_target(commands[0].target);if(error)return error;
    uint64_t emit_started=xenos_now_us();
    for(uint32_t i=0;i<count && !error;i++){
        const DrXenosTargetCommand *t=&commands[i];const DrXenosCommand *c=&t->command;
        error=dr_xenos_select_target(t->target);if(error)break;
        if(t->snapshot){
            if(t->snapshot==3){
                if(backend.used>ARENA_VERTICES-6){profile.arena_restarts++;error=dr_xenos_end();if(!error)error=dr_xenos_begin_target(t->target);if(error)break;}
                DrGsVertex quad[6];stencil_clear_vertices(quad,backend.width,backend.height);DrXenosDraw clear=stencil_clear_draw(backend.width,backend.height);error=dr_xenos_draw(quad,6,c->texture,&clear);
            }else error=dr_xenos_snapshot(t->snapshot-1);
            continue;
        }
        uint32_t unit=c->draw.topology==0?3:c->draw.topology==1?2:1,limit=MAX_VERTICES/unit*unit;
        for(uint32_t at=0;at<c->count && !error;){
            uint32_t n=c->count-at;if(n>limit)n=limit;
            if(n>ARENA_VERTICES-backend.used){profile.arena_restarts++;error=dr_xenos_end();if(!error)error=dr_xenos_begin_target(t->target);if(error)break;}
            /* The complete immutable list was checked above, with identical
             * chunk boundaries. Keep capacity, target readiness, snapshot and
             * GPU failure checks in draw_impl; avoid rescanning vertex floats. */
            const void *data=c->draw.vu_program?(const void *)(c->vu_vertices+at*12):(const void *)(c->vertices+at);
            error=draw_impl(data,n,c->texture,&c->draw,1);at+=n;
        }
    }
    profile.emit_us+=xenos_now_us()-emit_started;
    if(backend.active){int end=dr_xenos_end();if(!error)error=end;}
    return error;
}
