/* SPDX-License-Identifier: AGPL-3.0-or-later
 * API state/lifetime model, no rasterization or hardware execution. */
#define main previous_native_tests
#include "test_xenos.c"
#undef main
#include "dr_targets.h"
#include "dr_restore_shaders.h"
int main(void)
{
    /* D31 hardware regression: location 1 compiled as COLOR0, not TEXCOORD0. */
    XsdkShader probe;
    assert(NT_SUCCESS(xsdk_shader_load(&probe,dr_restore_vert.container,dr_restore_vert.container_bytes)));
    XsdkVertexElement layout[]={
        {.usage=XSDK_USAGE_POSITION,.format=XSDK_VERTEX_FLOAT4,.offset_bytes=0},
        {.usage=XSDK_USAGE_TEXCOORD,.format=XSDK_VERTEX_FLOAT2,.offset_bytes=16}};
    assert(!NT_SUCCESS(xsdk_shader_set_vertex_layout(&probe,layout,2,(const uint32_t[]){24})));
    layout[1].usage=XSDK_USAGE_COLOR;
    assert(NT_SUCCESS(xsdk_shader_set_vertex_layout(&probe,layout,2,(const uint32_t[]){24})));
    xsdk_shader_free(&probe);
    previous_native_tests();
    assert(dr_xenos_set_wait_timeout(0)==DR_XENOS_INVALID);
    assert(dr_xenos_set_wait_timeout(60001)==DR_XENOS_INVALID);
    assert(dr_xenos_set_wait_timeout(30000)==0);expected_wait_timeout=30000;
    assert(dr_xenos_open()==0);
    assert(dr_xenos_set_wait_timeout(1000)==DR_XENOS_STATE);
    uint32_t white;const uint8_t rgba[]={255,255,255,128};
    assert(dr_xenos_texture_create(1,1,rgba,&white)==0);
    assert(dr_xenos_target_create_gs(0,1024,1024)==0);
    assert(dr_xenos_target_create_gs(1,640,480)==0);
    assert(dr_targets_is_gs(0) && dr_targets_is_gs(1) && !dr_targets_is_gs(23));
    assert(dr_targets_texture(0,0) && dr_targets_texture(0,1) && dr_targets_texture(0,2));
    DrGsVertex v[3]={{0,0,.5f,128,128,128,128,0,0,1},
        {32,0,.5f,128,128,128,128,1,0,1},{0,32,.5f,128,128,128,128,0,1,1}};
    DrXenosDraw d={.logical_width=1024,.logical_height=1024,.depth=1,.depth_write=1,
        .scissor={0,0,640,480},.gs_material=1,.textured=1,.tcc=1,.fba=1,.stencil=1};
    model_enabled=1;
    assert(dr_xenos_begin_target(0)==0 && tile_recording && edram_color==0);
    d.date=1;unsigned before=draws;
    assert(dr_xenos_draw(v,3,white,&d)==DR_XENOS_STATE && draws==before);d.date=0;
    next_color=0x12345678;next_depth=.25f;next_aux=128;next_stencil=0xa5;
    assert(dr_xenos_draw(v,3,white,&d)==0);
    DrXenosProfile cache_before,cache_after;dr_xenos_profile(&cache_before);
    assert(dr_xenos_draw(v,3,white,&d)==0);dr_xenos_profile(&cache_after);
    assert(cache_after.literal_sets==cache_before.literal_sets);
    assert(cache_after.literal_skips>=cache_before.literal_skips+2);
    assert(cache_after.constant_registers==cache_before.constant_registers);
    assert(cache_after.constant_skips>cache_before.constant_skips);

    assert(cache_after.state_skips>=cache_before.state_skips+6);
    assert(cache_after.state_sets==cache_before.state_sets);

    /* SDK keeps count stale while its inline window advances. A near-full
     * cursor must split BEFORE a draw and preserve all four attachments. */
    unsigned split_passes=tile_passes;
    mock_command_cursor=mock_active_command->capacity_dwords-100;
    assert(mock_active_command->count==0);
    assert(dr_targets_ensure_commands(16384)==0 && tile_passes==split_passes+1);
    assert(edram_color==0x12345678 && edram_depth==.25f && edram_aux==128 && edram_stencil==0xa5);
    assert(dr_targets_ensure_commands(0)==DR_XENOS_INVALID);
    assert(dr_targets_ensure_commands(0x80001)==DR_XENOS_INVALID);
    mock_bad_cursor=1;assert(dr_targets_ensure_commands(16384)==DR_XENOS_GPU);mock_bad_cursor=0;
    mock_command_cursor=mock_active_command->capacity_dwords+1;
    assert(dr_targets_ensure_commands(16384)==DR_XENOS_GPU);
    mock_command_cursor=mock_active_command->capacity_dwords-16384;
    split_passes=tile_passes;assert(!dr_targets_ensure_commands(16384) && tile_passes==split_passes);
    mock_command_cursor=mock_active_command->capacity_dwords-100;
    assert(!dr_xenos_draw(v,3,white,&d) && tile_passes==split_passes+1);
    assert(edram_color==0x12345678 && edram_depth==.25f && edram_aux==128 && edram_stencil==0xa5);

    assert(dr_xenos_snapshot(0)==0 && tile_recording);
    const XsdkTexture *date=dr_targets_snapshot_texture(0,0,1024,1024);
    assert(date && images[date->address/65536].color==128);
    assert(date->width==1024 && date->height==1024 && date->pitch==1024);
    uint32_t snapshot_address=date->address;
    assert(!dr_targets_snapshot_texture(0,1,1024,1024));
    assert(edram_color==0x12345678 && edram_depth==.25f && edram_aux==128 && edram_stencil==0xa5);
    assert(stencil_restores>=9);
    d.date=2;next_color=0x87654321;next_aux=244;next_stencil=0x5a;
    assert(dr_xenos_draw(v,3,white,&d)==0);
    assert(fabsf(pixel_values[8]-1.0f/1024)<1e-8f && fabsf(pixel_values[9]-1.0f/1024)<1e-8f);
    assert(images[date->address/65536].color==128); // frozen, distinct from current aux
    assert(dr_xenos_snapshot(1)==0);
    const XsdkTexture *aux=dr_targets_snapshot_texture(1,0,1024,1024);
    assert(aux && aux!=date && images[aux->address/65536].color==244);
    assert(images[date->address/65536].color==128);
    d.date=0;
    assert(dr_xenos_select_target(1)==0 && edram_color==0 && edram_stencil==0);
    next_color=0x11223344;next_aux=127;next_depth=.75f;next_stencil=255;
    assert(dr_xenos_draw(v,3,white,&d)==0);
    assert(dr_xenos_select_target(0)==0 && edram_color==0x87654321 && edram_stencil==0x5a);
    assert(edram_aux==244 && edram_depth==.25f);
    unsigned passes=tile_passes;
    d.textured=2;d.texture_rect[2]=d.texture_rect[3]=1;
    assert(dr_xenos_draw(v,3,dr_xenos_target_texture(1),&d)==0 && tile_passes==passes);
    assert(bound_textures[0]->address==dr_targets_texture(1,0)->address);
    assert(bound_textures[2]->address==dr_targets_texture(1,1)->address);
    assert(bound_samplers[0]==bound_samplers[2]);
    assert(images[bound_textures[2]->address/65536].color==127);
    assert(dr_xenos_draw(v,3,dr_xenos_target_texture(0),&d)==0 && tile_passes>passes); // feedback is resolved before sampled
    assert(bound_textures[2]->address==dr_targets_texture(0,1)->address);
    before=draws;assert(dr_xenos_draw(v,3,white,&d)==DR_XENOS_INVALID && draws==before);
    assert(dr_xenos_end()==0 && !tile_recording);
    /* Real literal layouts overlap model registers when the CPU GS shader
     * writes register 1. Switching back must restore the bone row. */
    assert(dr_xenos_begin_target(0)==0);
    float raw[36]={0},uniforms[76]={0};uniforms[70]=65535;
    DrXenosDraw model={.logical_width=1024,.logical_height=1024,.depth=1,.depth_write=1,
        .scissor={0,0,640,480},.gs_material=1,.textured=1,.tcc=1,.vu_program=1,.vu_uniform=uniforms};
    assert(dr_xenos_draw_vu(raw,3,white,&model)==0);dr_xenos_profile(&cache_before);
    assert(dr_xenos_draw_vu(raw,3,white,&model)==0);dr_xenos_profile(&cache_after);
    assert(cache_after.constant_registers==cache_before.constant_registers);
    uniforms[71]=1;dr_xenos_profile(&cache_before);
    assert(dr_xenos_draw_vu(raw,3,white,&model)==0);dr_xenos_profile(&cache_after);
    assert(cache_after.constant_registers-cache_before.constant_registers==3);
    DrXenosDraw cpu=model;cpu.vu_program=0;cpu.vu_uniform=NULL;
    assert(dr_xenos_draw(v,3,white,&cpu)==0 && vu_constants[4]==1);
    assert(dr_xenos_draw_vu(raw,3,white,&model)==0 && vu_constants[4]==0);
    dr_xenos_profile(&cache_before);assert(dr_xenos_snapshot(0)==0);
    assert(dr_xenos_draw_vu(raw,3,white,&model)==0);dr_xenos_profile(&cache_after);
    assert(cache_after.literal_sets>cache_before.literal_sets);
    assert(dr_xenos_end()==0);
    unsigned frees=texture_frees;
    wait_result=STATUS_IO_TIMEOUT;
    assert(dr_xenos_target_drop(0)==DR_XENOS_GPU && texture_frees==frees);
    wait_result=STATUS_SUCCESS;
    assert(dr_xenos_target_drop(0)==0 && !dr_targets_snapshot_texture(0,0,1024,1024));
    assert(dr_xenos_target_create_gs(0,64,64)==0);
    assert(dr_xenos_begin_target(0)==0);
    unsigned created=texture_creates;
    assert(dr_xenos_snapshot(0)==0);
    date=dr_targets_snapshot_texture(0,0,64,64);
    assert(date && date->pitch==64 && date->width==64 && date->height==64);
    assert(date->address==snapshot_address && texture_creates==created);
    tile_end_result=STATUS_BUFFER_TOO_SMALL;
    assert(dr_xenos_end()==DR_XENOS_GPU && !tile_recording);
    tile_end_result=STATUS_SUCCESS;
    assert(dr_xenos_close()==0);model_enabled=0;
    assert(dr_xenos_set_wait_timeout(1000)==0);expected_wait_timeout=1000;
    for(unsigned i=0;i<4096;i++) assert(!images[i].live);
    puts("PASS: GS MRT/tile call model, A/B/A color/raw-alpha/depth/stencil, frozen DATE/effect snapshots, target sampling/feedback, timeout ownership, failed recording cleanup; NO GPU execution");
}
