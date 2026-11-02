/* SPDX-License-Identifier: AGPL-3.0-or-later
 * CPU call-order/lifetime model only, NOT a GPU/shader rasterizer. */
#define main previous_lifetime_tests
#include "test_xenos.c"
#undef main
#include "dr_targets.h"
int main(void)
{
    previous_lifetime_tests();
    assert(dr_xenos_open()==0);
    uint32_t white;const uint8_t rgba[4]={255,255,255,128};
    assert(dr_xenos_texture_create(1,1,rgba,&white)==0);
    assert(dr_xenos_target_create(0,640,480)==0);
    assert(dr_xenos_target_create(1,320,240)==0);
    unsigned made=texture_creates;
    assert(dr_xenos_target_create(0,640,480)==0 && texture_creates==made);
    assert(dr_xenos_target_create(24,640,480)==DR_XENOS_INVALID);
    assert(dr_xenos_target_create(2,641,480)==DR_XENOS_INVALID);
    assert(dr_xenos_target_create(2,1280,1025)==DR_XENOS_INVALID);
    model_enabled=1;edram_color=0xdeadbeef;edram_depth=.9f;
    assert(dr_xenos_begin_target(0)==0 && edram_color==0 && edram_depth==0);
    DrGsVertex v[3]={{0,0,.5f,128,128,128,128,0,0,1},
        {32,0,.5f,128,128,128,128,1,0,1},{0,32,.5f,128,128,128,128,0,1,1}};
    DrXenosDraw d={.logical_width=640,.logical_height=480,.depth=1,.depth_write=1,.scissor={0,0,320,240}};
    next_color=0xff334455;next_depth=.25f;
    assert(dr_xenos_draw(v,3,white,&d)==0);
    assert(dr_xenos_select_target(1)==0 && edram_color==0 && edram_depth==0);
    next_color=0xffabcdef;next_depth=.75f;assert(dr_xenos_draw(v,3,white,&d)==0);
    assert(dr_xenos_select_target(0)==0 && edram_color==0xff334455 && edram_depth==.25f);
    unsigned calls=draws;assert(dr_xenos_select_target(0)==0 && draws==calls);
    /* Rejecting an invalid ID must preserve the active surface. */
    assert(dr_xenos_select_target(23)==DR_XENOS_INVALID && edram_color==0xff334455);
    assert(dr_xenos_target_drop(1)==DR_XENOS_STATE && dr_xenos_target_create(2,64,64)==DR_XENOS_STATE);
    assert(dr_xenos_end()==0);
    assert(dr_xenos_begin_target(1)==0 && edram_color==0xffabcdef && edram_depth==.75f);
    assert(dr_xenos_end()==0);
    /* A normal diagnostic batch can overwrite eDRAM; saved targets survive. */
    assert(dr_xenos_begin(640,480)==0);next_color=99;next_depth=.1f;
    assert(dr_xenos_draw(v,3,white,&d)==0 && dr_xenos_end()==0);
    assert(dr_xenos_begin_target(0)==0 && edram_color==0xff334455 && edram_depth==.25f);
    assert(dr_xenos_end()==0);
    /* Partial resize allocation failure keeps the OLD target and its pixels. */
    texture_failure=texture_creates+2;unsigned f=texture_frees;
    assert(dr_xenos_target_create(0,800,600)==DR_XENOS_GPU && texture_frees==f+1);
    texture_failure=0;uint32_t w,h;assert(dr_targets_size(0,&w,&h)==0 && w==640 && h==480);
    assert(dr_xenos_begin_target(0)==0 && edram_color==0xff334455 && dr_xenos_end()==0);
    wait_result=STATUS_IO_TIMEOUT;f=texture_frees;unsigned a=freed;
    assert(dr_xenos_target_drop(0)==DR_XENOS_GPU && texture_frees==f);
    assert(dr_xenos_close()==DR_XENOS_GPU && texture_frees==f && freed==a);
    wait_result=STATUS_SUCCESS;assert(dr_xenos_target_drop(0)==0 && texture_frees==f+2);
    assert(dr_xenos_target_drop(0)==0);
    assert(dr_xenos_target_create(0,64,16)==0); /* small texture must be moved out of packed tail */
    assert(dr_xenos_begin_target(0)==0 && edram_color==0 && dr_xenos_end()==0);
    assert(dr_xenos_close()==0);model_enabled=0;
    /* A complete pool obeys 128 MiB including replacement peak and page rounding. */
    assert(dr_xenos_open()==0);
    for(unsigned i=0;i<16;i++) assert(dr_xenos_target_create(i,1024,1024)==0);
    made=texture_creates;
    assert(dr_xenos_target_create(16,1024,1024)==DR_XENOS_FULL && texture_creates==made);
    assert(dr_xenos_target_create(0,512,512)==DR_XENOS_FULL && texture_creates==made);
    assert(dr_xenos_target_drop(15)==0 && dr_xenos_target_create(0,512,512)==0);
    assert(dr_xenos_close()==0);
    for(unsigned i=0;i<4096;i++) assert(!images[i].live);
    puts("PASS: native target A/B/A call model, color/depth lifecycle, failure-safe resize, timeout ownership, page budget; no GPU execution");
}
