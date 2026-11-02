/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Call/lifetime mocks only. Does not rasterize shaders or emulate Xenos. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "dr_xenos.h"
#include "dr_graphics_demo.h"
#include "dr_effects_shaders.h"
#include "xsdk/gpu.h"
static uint32_t arena[DR_XENOS_FRAME_VERTICES*12];
static uint32_t restore_quad[36];
static const XsdkShader *outline_pixel,*clut_pixel,*current_pixel;
static unsigned outline_draws,clut_draws;
static uint32_t outline_aux_address,clut_aux_address;
static const XsdkShader *restore_vertex,*vu_vertices[3],*current_vertex;
static unsigned vu_layouts,vu_draws,shadow_clears,shadow_draws;
static float vu_constants[76];
static unsigned texture_creates,texture_failure,texture_frees;
static struct {int live;uint32_t color,aux;float depth;uint8_t stencil;} images[4096];
static const XsdkTexture *bound_textures[32];
static uint32_t bound_samplers[32], sampler_history[4096],sampler_calls;
static uint32_t edram_color,next_color;
static float edram_depth,next_depth;
static int restore_draw,model_enabled,target_ready,clear_color=1,clear_depth=1;
static unsigned draws,freed,waits,fences,shader_loads,shader_failure;
static uint32_t vertex_address,vertex_bytes,last_primitive;
static unsigned char uploaded_pixel[4];
static float material_constants[8];
static float pixel_values[16];
static XsdkBlend last_blend;
static unsigned mip_uploads;
static const XsdkShader *loaded_shaders[4096],*gs_restore_pixel,*gs_stencil_pixel;
static int tile_recording;
static uint32_t tile_width,tile_height;
static unsigned tile_passes,stencil_restores;
static uint32_t edram_aux,next_aux;
static uint8_t edram_stencil,next_stencil;
static float stencil_bit;
static XsdkDepthStencil last_depth_stencil;
static XsdkBlendState last_mrt_blends[2];
static NTSTATUS tile_end_result=STATUS_SUCCESS;
static uint32_t mock_recording[32][1];
static unsigned mock_command_live[32],mock_command_frees,mock_command_creates,mock_command_failure;
static const XsdkCommandBuffer *mock_active_command;
static uint32_t mock_command_cursor;
static int mock_bad_cursor;
volatile uint32_t *xsdk_gpu_reserve(uint32_t n) {
 assert(n==0 && tile_recording);
 return (volatile uint32_t *)((uintptr_t)mock_active_command->dwords+(mock_bad_cursor?1:mock_command_cursor*4u));
}
static NTSTATUS wait_result=STATUS_SUCCESS;
NTSTATUS xsdk_shader_load(XsdkShader *s,const uint32_t *code,uint32_t bytes)
{assert(code && bytes>64);shader_loads++;if(shader_loads==shader_failure)return STATUS_NO_MEMORY;assert(shader_loads<4096);loaded_shaders[shader_loads]=s;
 if(bytes==dr_outline_frag.container_bytes && !memcmp(code,dr_outline_frag.container,bytes))outline_pixel=s;
 if(bytes==dr_dclut_frag.container_bytes && !memcmp(code,dr_dclut_frag.container,bytes))clut_pixel=s;
 memset(s,0,sizeof(*s));
 /* Preserve the real container fetch contract instead of accepting every layout. */
 if((code[0]&255u)==1u) {
  uint32_t record=code[6]/4;assert(record+8<=bytes/4);
  uint32_t fetch=record+8+code[record+6]*2;
  s->fetch_count=code[record+7];assert(fetch+s->fetch_count<=bytes/4);
  s->fetches=code+fetch;
 }
 return STATUS_SUCCESS;}
NTSTATUS xsdk_shader_set_vertex_layout(XsdkShader *s,const XsdkVertexElement *e,uint32_t n,const uint32_t *stride)
{
 for(uint32_t i=0;i<s->fetch_count;i++) {
  uint32_t usage=(s->fetches[i]>>12)&15,index=(s->fetches[i]>>16)&15;
  bool found=false;
  for(uint32_t j=0;j<n;j++)if(e[j].usage==usage && e[j].usage_index==index)found=true;
  if(!found)return STATUS_INVALID_PARAMETER;
 }
 if(n==2) {assert(e[0].format==XSDK_VERTEX_FLOAT4 && stride[0]==24);restore_vertex=s;}
 else {assert(n==3 && e[1].format==XSDK_VERTEX_FLOAT4 && stride[0]==48);if(e[0].format==XSDK_VERTEX_FLOAT4)vu_vertices[vu_layouts++%3]=s;}return STATUS_SUCCESS;}
void xsdk_shader_free(XsdkShader *s) {(void)s;}
volatile uint32_t *xsdk_gpu_alloc(uint32_t n) {assert(n<=sizeof(arena));return n==144?restore_quad:arena;}
void xsdk_gpu_free(volatile uint32_t *p) {assert(p==arena || p==restore_quad);freed++;}
uint32_t xsdk_gpu_address(volatile const uint32_t *p) {assert(p==arena || p==restore_quad);return p==arena?0x100000:0x300000;}
void xsdk_gpu_invalidate(uint32_t a,uint32_t n) {assert(a && n);}
static uint32_t expected_wait_timeout=1000;
NTSTATUS xsdk_gpu_wait(uint32_t f,uint32_t t) {assert(!tile_recording && f && t==expected_wait_timeout);waits++;return wait_result;}
uint32_t xsdk_gpu_fence(void) {assert(!tile_recording);return ++fences;}
NTSTATUS xsdk_texture_init(XsdkTexture *t,uint32_t format,uint32_t w,uint32_t h,uint32_t levels,bool tiled)
{assert((format==XSDK_FORMAT_R8G8B8A8 || format==XSDK_FORMAT_A8R8G8B8 || format==XSDK_FORMAT_D24S8) && w && h && levels>=1 && levels<=11 && tiled);
 memset(t,0,sizeof(*t));t->format=format;t->width=w;t->height=h;t->levels=levels;t->tiled=true;
 t->pitch=w;t->size_bytes=((w*((h+31)&~31u)*4+65535)&~65535u);return STATUS_SUCCESS;}
NTSTATUS xsdk_texture_create(XsdkTexture *t,uint32_t format,uint32_t w,uint32_t h,uint32_t levels,bool tiled)
{if(++texture_creates==texture_failure)return STATUS_NO_MEMORY;
 assert(texture_creates<4096);xsdk_texture_init(t,format,w,h,levels,tiled);
 t->address=texture_creates*65536;t->texels=(volatile uint32_t *)(uintptr_t)t->address;images[texture_creates].live=1;return STATUS_SUCCESS;}
void xsdk_texture_attach(XsdkTexture *t,volatile uint32_t *p) {t->texels=p;t->address=(uint32_t)(uintptr_t)p;}
void xsdk_texture_base_at_corner(XsdkTexture *t) {assert(t->tiled && t->levels==1);}
void xsdk_texture_upload(XsdkTexture *t,uint32_t l,const void *p,uint32_t row) {assert(l<t->levels && p && row);mip_uploads++;memcpy(uploaded_pixel,p,4);}
void xsdk_texture_free(XsdkTexture *t) {unsigned i=t->address/65536;assert(i<4096 && images[i].live);images[i].live=0;texture_frees++;}
void xsdk_texture_sampler_state(XsdkSamplerState *s,const XsdkSampler *setting) {assert(setting->mag_filter<=XSDK_FILTER_LINEAR);
 s->fetch[0]=setting->mag_filter | (setting->address_u<<4) | (setting->address_v<<8) | (setting->level_count<<12);}
void xsdk_gpu_set_target(uint32_t w,uint32_t h) {assert(w && h);target_ready=1;}
uint32_t xsdk_gpu_target_tiles(uint32_t w,uint32_t h) {return (((((w+31)&~31u)+79)/80+1)&~1u)*((h+15)/16);}
void xsdk_gpu_set_depth_clear(float d,uint8_t s) {assert(d==0 && s==0);}
void xsdk_gpu_set_resolve_clears(bool c,bool d) {clear_color=c;clear_depth=d;}
void xsdk_gpu_resolve_texture(const XsdkTexture *t,uint32_t argb)
{assert(target_ready && t->format==XSDK_FORMAT_R8G8B8A8 && !argb);images[t->address/65536].color=edram_color;
 if(clear_color) {edram_color=0;}if(clear_depth) {edram_depth=0;}target_ready=0;}
void xsdk_gpu_resolve_depth_texture(const XsdkTexture *t)
{assert(target_ready && t->format==XSDK_FORMAT_D24S8);images[t->address/65536].depth=edram_depth;
 if(clear_depth) {edram_depth=0;}target_ready=0;}
void xsdk_gpu_depth_stencil_state(XsdkDepthStencilState *s,const XsdkDepthStencil *d) {(void)s;assert(d->depth_test);last_depth_stencil=*d;}
void xsdk_gpu_set_depth_stencil(const XsdkDepthStencilState *s) {(void)s;}
void xsdk_gpu_blend_state(XsdkBlendState *s,const XsdkBlend *b) {assert(b->write_mask<=15);memset(s,0,sizeof(*s));s->color_mask=b->write_mask;s->blend_control=b->color_op;last_blend=*b;}
void xsdk_gpu_set_blend(const XsdkBlendState *s) {(void)s;}
void xsdk_gpu_set_blends(const XsdkBlendState *s,uint32_t count) {assert(s && count==2);memcpy(last_mrt_blends,s,sizeof(last_mrt_blends));}
void xsdk_gpu_set_blend_constants(const float rgba[4]) {assert(rgba && isfinite(rgba[0]));}
void xsdk_gpu_set_scissor(uint32_t x,uint32_t y,uint32_t w,uint32_t h) {(void)x;(void)y;assert(w && h);}
void xsdk_gpu_set_shaders(const XsdkShader *v,const XsdkShader *p) {assert(v && p);current_vertex=v;current_pixel=p;restore_draw=v!=restore_vertex?0:p==gs_stencil_pixel?2:p==gs_restore_pixel?3:1;}
void xsdk_gpu_set_vertex_constants(uint32_t f,const float *v,uint32_t n) {(void)f;assert(v && n);static float bank[256*4];assert(f+n<=256);memcpy(bank+f*4,v,n*16);memcpy(vu_constants,bank,sizeof(vu_constants));}
void xsdk_gpu_set_pixel_constants(uint32_t f,const float *v,uint32_t n) {assert(v && n);if(f==0){memcpy(pixel_values,v,(n<4?n:4)*16);if(n>=2)memcpy(material_constants,v,sizeof(material_constants));}if(f==0 && n==1 && restore_draw==2)stencil_bit=v[0];}
void xsdk_gpu_set_texture(uint32_t n,const XsdkTexture *t,const XsdkSamplerState *s) {assert(n<32 && t && s);bound_textures[n]=t;bound_samplers[n]=s->fetch[0];if(n==0 && sampler_calls<4096)sampler_history[sampler_calls++]=s->fetch[0];}
void xsdk_gpu_set_vertices(uint32_t stream,uint32_t address,uint32_t bytes) {assert(!stream);vertex_address=address;vertex_bytes=bytes;}
void xsdk_gpu_draw(uint32_t primitive,uint32_t count) {assert(count);draws++;for(unsigned j=0;j<3;j++)if(current_vertex==vu_vertices[j])vu_draws++;last_primitive=primitive;
 if(current_pixel==outline_pixel) {
  assert(fabsf(pixel_values[0]-1.0f/bound_textures[0]->width)<1e-8f);
  assert(fabsf(pixel_values[1]-1.0f/bound_textures[0]->height)<1e-8f);
  assert(!last_depth_stencil.depth_write && last_depth_stencil.depth_compare==XSDK_COMPARE_ALWAYS);
  assert(last_mrt_blends[0].color_mask==7 && !last_mrt_blends[1].color_mask);
  assert(last_mrt_blends[0].blend_control==XSDK_BLEND_OP_REVERSE_SUBTRACT);
  outline_aux_address=bound_textures[0]->address;outline_draws++;
 }
 if(current_pixel==clut_pixel) {
  assert(fabsf(pixel_values[2]-1.0f/bound_textures[1]->width)<1e-8f);
  assert(fabsf(pixel_values[3]-1.0f/bound_textures[1]->height)<1e-8f);
  assert(!last_depth_stencil.depth_write && bound_textures[0]->format==XSDK_FORMAT_D24S8);
  assert(bound_textures[2]->width==256 && bound_textures[2]->height==1);
  assert(last_mrt_blends[1].color_mask==((last_mrt_blends[0].color_mask&8)?1u:0u));
  clut_aux_address=bound_textures[1]->address;clut_draws++;
 }
 if(!restore_draw && last_depth_stencil.stencil_test) {
  if(last_depth_stencil.stencil_pass==XSDK_STENCIL_REPLACE) {
   assert(!last_depth_stencil.depth_write && last_depth_stencil.stencil_reference==0);
   assert(!last_mrt_blends[0].color_mask && !last_mrt_blends[1].color_mask);
   shadow_clears++;
  } else if(last_depth_stencil.stencil_pass==XSDK_STENCIL_INCREMENT_CLAMP) shadow_draws++;
 }
 if(model_enabled) {assert(target_ready);if(restore_draw) {
  assert(count==6 && bound_textures[0]);
  if(restore_draw==2) {
   if(stencil_bit==0 || (images[bound_textures[0]->address/65536].stencil&(uint8_t)stencil_bit))
    edram_stencil=(edram_stencil&~last_depth_stencil.stencil_write_mask)|last_depth_stencil.stencil_reference;
   stencil_restores++;
  } else {
   assert(bound_textures[1]);edram_color=images[bound_textures[0]->address/65536].color;
   edram_depth=images[bound_textures[1]->address/65536].depth;
   if(restore_draw==3) edram_aux=images[bound_textures[2]->address/65536].color;
  }
 } else {edram_color=next_color;edram_depth=next_depth;if(tile_recording) {if(last_mrt_blends[1].color_mask&1)edram_aux=next_aux;edram_stencil=next_stencil;}}}}
NTSTATUS xsdk_gpu_command_buffer_create(XsdkCommandBuffer *b,uint32_t capacity)
{assert(capacity==0xfffff);memset(b,0,sizeof(*b));if(++mock_command_creates==mock_command_failure)return STATUS_NO_MEMORY;unsigned i=0;while(i<32&&mock_command_live[i])i++;assert(i<32);mock_command_live[i]=1;b->dwords=mock_recording[i];b->capacity_dwords=capacity;b->address=0x400000;
 gs_restore_pixel=loaded_shaders[shader_loads-1];gs_stencil_pixel=loaded_shaders[shader_loads];return STATUS_SUCCESS;}
void xsdk_gpu_command_buffer_free(XsdkCommandBuffer *b) {assert(!tile_recording);unsigned i=0;while(i<32&&b->dwords!=mock_recording[i])i++;assert(i<32&&mock_command_live[i]);mock_command_live[i]=0;mock_command_frees++;memset(b,0,sizeof(*b));}
NTSTATUS xsdk_gpu_begin_tiles(const XsdkTiling *t,XsdkCommandBuffer *b)
{assert(!tile_recording && t->samples==1 && t->count==2 && t->depth && b->dwords);tile_recording=1;tile_width=t->width;tile_height=t->height;tile_passes++;b->count=0;mock_command_cursor=0;mock_active_command=b;
 target_ready=1;edram_color=edram_aux=edram_stencil=0;edram_depth=0;return STATUS_SUCCESS;}
NTSTATUS xsdk_gpu_end_tiles(const XsdkTileResolve *r,uint32_t count)
{assert(tile_recording && (count==3 || count==4));tile_recording=0;target_ready=0;
 if(!NT_SUCCESS(tile_end_result)) return tile_end_result;
 for(unsigned i=0;i<count;i++) {
  assert(r[i].texture->width==tile_width && r[i].texture->height==tile_height && r[i].texture->pitch==tile_width);
  unsigned image=r[i].texture->address/65536;assert(image<4096 && images[image].live);
  if(r[i].target==XSDK_TILE_DEPTH) {images[image].depth=edram_depth;images[image].stencil=edram_stencil;}
  else images[image].color=r[i].target==0?edram_color:edram_aux;
 }
 edram_color=edram_aux=edram_stencil=0;edram_depth=0;return STATUS_SUCCESS;}
int main(void)
{
    uint32_t texture=99;unsigned char rgba[]={255,255,255,128};
    DrGsVertex v[3]={{0,0,.5f,128,128,128,128,0,0,1},{32,0,.5f,128,128,128,128,1,0,1},{0,32,.5f,128,128,128,128,0,1,1}};
    DrXenosDraw d={.logical_width=32,.logical_height=32,.depth=1,.scissor={0,0,640,480}};
    assert(dr_xenos_begin(640,480)==DR_XENOS_STATE);
    assert(dr_xenos_open()==0 && dr_xenos_open()==DR_XENOS_STATE);
    assert(dr_xenos_texture_create(1,1,rgba,&texture)==0 && texture==1);
    assert(dr_xenos_begin(641,480)==DR_XENOS_INVALID);
    assert(dr_xenos_begin(640,480)==0 && dr_xenos_begin(640,480)==DR_XENOS_STATE);
    assert(dr_xenos_close()==DR_XENOS_STATE);
    assert(dr_xenos_texture_create(1,1,rgba,&texture)==DR_XENOS_STATE);
    assert(dr_xenos_draw(v,3,1,&d)==0 && draws==1 && vertex_address==0x100000 && vertex_bytes==144);
    uint32_t saved[36];memcpy(saved,arena,sizeof(saved));
    d.blend=1;assert(dr_xenos_draw(v,3,1,&d)==0 && vertex_address==0x100000+144);
    assert(!memcmp(saved,arena,sizeof(saved))); /* Earlier draw is not overwritten. */
    unsigned before=draws;
    d.blend=3;assert(dr_xenos_draw(v,3,1,&d)==DR_XENOS_INVALID);d.blend=0;
    assert(dr_xenos_draw(v,2,1,&d)==DR_XENOS_INVALID);
    assert(dr_xenos_draw(v,DR_XENOS_VERTEX_CAPACITY+1,1,&d)==DR_XENOS_FULL);
    assert(dr_xenos_draw(v,3,0,&d)==DR_XENOS_INVALID);
    d.scissor[2]=641;assert(dr_xenos_draw(v,3,1,&d)==DR_XENOS_INVALID);d.scissor[2]=640;
    v[2].x=NAN;assert(dr_xenos_draw(v,3,1,&d)==DR_XENOS_INVALID);v[2].x=0;
    v[1].q=0;assert(dr_xenos_draw(v,3,1,&d)==DR_XENOS_INVALID);v[1].q=1;
    assert(draws==before);
    assert(dr_xenos_end()==0 && dr_xenos_end()==DR_XENOS_STATE);
    wait_result=STATUS_IO_TIMEOUT;
    assert(dr_xenos_begin(640,480)==DR_XENOS_GPU);
    assert(dr_xenos_close()==DR_XENOS_GPU && !freed); /* Never free while timed out. */
    wait_result=STATUS_SUCCESS;assert(dr_xenos_begin(640,480)==0);
    d.topology=1;assert(!dr_xenos_draw(v,2,1,&d) && last_primitive==XSDK_PRIMITIVE_LINE_LIST);
    d.topology=2;assert(!dr_xenos_draw(v,1,1,&d) && last_primitive==XSDK_PRIMITIVE_POINT_LIST);
    assert(dr_xenos_end()==0 && dr_xenos_close()==0 && freed==1 && waits>=3);
    shader_failure=shader_loads+2;assert(dr_xenos_open()==DR_XENOS_GPU);
    shader_failure=0;assert(dr_xenos_open()==0);
    for(unsigned i=0;i<256;i++) assert(dr_xenos_texture_create(1,1,rgba,&texture)==0 && texture==i+1);
    assert(dr_xenos_texture_create(1,1,rgba,&texture)==DR_XENOS_FULL && texture==0);
    assert(dr_xenos_close()==0 && freed==2);
    before=draws;
    assert(dr_graphics_demo_open()==0);
    assert(!memcmp(uploaded_pixel,"\xff\x8a\x1f\x80",4));
    /* D41 skips nine zero-stencil restores: 4 real draws + one MRT restore. */
    assert(dr_graphics_demo_draw(640,480)==0 && draws==before+5);
    assert(dr_graphics_demo_close()==0 && freed==4);
    puts("PASS: PSMT8 palette diagnostic chain and native renderer calls, vertex arena lifetime, fences/timeouts, failure cleanup, state/capacity/input checks");
    return 0;
}
