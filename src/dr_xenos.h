/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef DR_XENOS_H
#define DR_XENOS_H
#include <stdint.h>
#define DR_XENOS_VERTEX_CAPACITY 1024u
#define DR_XENOS_FRAME_VERTICES 262144u
/* Same field order as upstream Vtx; CPU-produced, not a file representation. */
typedef struct {float x,y,z;uint8_t r,g,b,a;float s,t,q;} DrGsVertex;
typedef struct {
    uint32_t logical_width,logical_height;
    uint32_t topology; /* 0 triangles, 1 lines, 2 points */
    uint32_t blend; /* 0 opaque, 1 source-alpha, 2 additive source-alpha */
    uint32_t depth; /* GS ZTST: 0 never, 1 always, 2 >=, 3 > */
    uint32_t depth_write;
    int scissor[4]; /* physical target pixels, x/y/width/height */
    uint32_t gs_material; /* 1: use TFX/TCC/ATST below; 0: legacy MODULATE/TCC */
    uint32_t textured,tfx,tcc,alpha_test,alpha_reference;
    uint32_t gs_blend; /* 1: use the upstream blend key and channel mask */
    uint32_t blend_key,write_mask;
    float blend_constant;
    uint32_t date,fba; /* DATE: 0 off, 1 bit7 clear, 2 bit7 set. FBA sets stored bit7. */
    uint32_t stencil; /* 1 shadow overlap: ==0/increment; 2 stencil-only clear pass */
    float texture_rect[4]; /* sampled-target UV clamp bounds, for textured=2 */
    uint32_t effect; /* 0 normal, 1 outline from frozen IDs, 2 depth/ID CLUT */
    uint32_t effect_source; /* CLUT: 0 fog from depth, 1 frozen raw alpha */
    float effect_depth_max;
    uint32_t vu_program; /* 0 CPU GS vertices; 1 model, 2 stage, 3 shadow projection */
    const float *vu_uniform; /* 76 CPU-native floats, Vu0Uniform; valid until replay finishes */
    float depth_quantize; /* 0 fractional; otherwise 65535, 16777215 or 4294967295 */
    uint32_t sampler; /* upstream: bit0 linear, bit1 clamp U, bit2 clamp V, bit3 permit mips */
    uint32_t sprite_clamp; /* original smooth 2D art: inset rectangle in texture pixels */
    uint32_t preserve_aux; /* color-only targetCopy must leave raw alpha unchanged */
} DrXenosDraw;
typedef struct {const void *pixels;uint32_t bytes;} DrXenosMip;
enum { DR_XENOS_OK=0,DR_XENOS_INVALID=-100,DR_XENOS_FULL=-101,DR_XENOS_GPU=-102,DR_XENOS_STATE=-103 };
typedef struct {
    const DrGsVertex *vertices;
    uint32_t count,texture;
    DrXenosDraw draw;
    const float *vu_vertices; /* count * 12 floats; exclusive with vertices */
} DrXenosCommand;
typedef struct {unsigned target;DrXenosCommand command;unsigned snapshot; /* 1 DATE, 2 aux, 3 stencil clear */} DrXenosTargetCommand;
/* Experimental native drawing core, NOT a complete GsBackend or SDL_GPU driver.
 * Caller owns xsdk_gpu_open/present. One thread and one active batch at a time.
 * At most 1024 vertices per batch and 256 textures. Source RGBA keeps GS alpha.
 * TFX/TCC/ATST and GS blend keys are available; DATE/FBA on GS surfaces; indexed model/stage/shadow VU shaders; native outline/depth-ID CLUT consumers; no full GS emulation.
 * Saved native color/depth surfaces are available through the target API below.
 * A bounded 12 MiB frame arena keeps earlier draws immutable across batches;
 * begin waits for the previous fence before reusing this arena. */
typedef struct {uint32_t draws,arena_restarts,fence_waits,literal_sets,literal_skips,constant_registers,constant_skips,validated_vertices,reused_validation_vertices,state_sets,state_skips;
    /* Lifetime replay-path stage timings (replay_targets only) and counts. */
    uint64_t preflight_us,emit_us,select_us;uint32_t target_selects,target_snapshots;} DrXenosProfile;
void dr_xenos_profile(DrXenosProfile *);
/* Closed renderer only, 1..60000 ms. Default 1000; Xenia's software GPU
 * diagnostic selects 30000 explicitly. A timeout never releases live memory. */
int dr_xenos_set_wait_timeout(uint32_t milliseconds);
int dr_xenos_open(void);
int dr_xenos_texture_create(uint32_t width,uint32_t height,const void *rgba,uint32_t *handle);
int dr_xenos_texture_allocate(uint32_t width,uint32_t height,uint32_t levels,uint32_t *handle);
int dr_xenos_texture_upload(uint32_t handle,uint32_t level,const void *rgba,uint32_t bytes);
int dr_xenos_texture_destroy(uint32_t handle);
uint32_t dr_xenos_texture_slots(void);
/* Explicit RGBA mip levels, each half the previous size, clamped to 1.
 * Checks the whole chain before allocation. Does not generate GS mip data. */
int dr_xenos_texture_create_mips(uint32_t width,uint32_t height,const DrXenosMip *mips,uint32_t levels,uint32_t *handle);
int dr_xenos_begin(uint32_t physical_width,uint32_t physical_height);
/* Saved color/depth surfaces, experimental and not a full GsBackend. Creation /
 * destruction only outside a batch. begin_target starts a batch; select switches
 * within it; end saves it. Use create_gs for MRT raw alpha, snapshots and eight-bit stencil restoration;
 * legacy create preserves color/depth only. */
int dr_xenos_target_create(unsigned id,uint32_t width,uint32_t height);
int dr_xenos_target_create_gs(unsigned id,uint32_t width,uint32_t height);
/* Frozen alpha snapshots, outside primitive draws but within a GS batch. */
int dr_xenos_snapshot(unsigned kind);
/* Target color handle usable as a texture; captures the source before sampling. */
uint32_t dr_xenos_target_texture(unsigned id);
int dr_xenos_target_drop(unsigned id);
int dr_xenos_begin_target(unsigned id);
int dr_xenos_select_target(unsigned id);
int dr_xenos_draw(const DrGsVertex *,uint32_t count,uint32_t texture,const DrXenosDraw *);
/* Validates the complete list before emitting any draw. An active batch is required.
 * This is CPU-side validation, not an atomic GPU transaction. */
int dr_xenos_draw_list(const DrXenosCommand *,uint32_t count);
/* Owns begin/select/end, with complete preflight before touching eDRAM.
 * Commands, vertex arrays and uniforms must remain immutable until return.
 * Emission reuses this checked data; public standalone draw calls still validate. */
int dr_xenos_replay_targets(const DrXenosTargetCommand *,uint32_t count);
int dr_xenos_draw_vu(const float *vertices,uint32_t count,uint32_t texture,const DrXenosDraw *);
int dr_xenos_end(void);
int dr_xenos_close(void);
int dr_xenos_sync(void);
#endif
