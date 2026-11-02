/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Native API validation and command traces; no GPU execution. */
#define main previous_native_tests
#include "test_xenos.c"
#undef main
int main(void)
{
    previous_native_tests();
    assert(dr_xenos_open()==0);
    unsigned char pixels[64]={0};
    DrXenosMip mips[]={{pixels,64},{pixels,16},{pixels,4},{pixels,4}};
    uint32_t handle;unsigned created=texture_creates,uploads=mip_uploads;
    assert(dr_xenos_texture_create_mips(4,4,mips,4,&handle)==DR_XENOS_INVALID && !handle);
    assert(texture_creates==created && mip_uploads==uploads);
    mips[2].bytes=3;
    assert(dr_xenos_texture_create_mips(4,4,mips,3,&handle)==DR_XENOS_INVALID);
    mips[2].bytes=4;mips[2].pixels=NULL;
    assert(dr_xenos_texture_create_mips(4,4,mips,3,&handle)==DR_XENOS_INVALID);
    mips[2].pixels=pixels;
    assert(dr_xenos_texture_create_mips(4,4,mips,3,&handle)==0 && handle==1);
    assert(texture_creates==created+1 && mip_uploads==uploads+3);
    DrGsVertex v[3]={{0,0,.5f,128,128,128,128,0,0,1},
        {32,0,.5f,128,128,128,128,1,0,1},{0,32,.5f,128,128,128,128,0,1,1}};
    DrXenosDraw d={.logical_width=640,.logical_height=480,.depth=1,.scissor={0,0,640,480},
        .gs_material=1,.textured=1,.tfx=3,.tcc=0,.alpha_test=8,.alpha_reference=127,
        .gs_blend=1,.write_mask=5,.blend_constant=.5f};
    assert(dr_xenos_begin(640,480)==0);
    for(unsigned c=0;c<3;c++) for(unsigned src=0;src<6;src++) for(unsigned dst=0;dst<6;dst++) {
        d.blend_key=1|(src<<1)|(dst<<4)|(c<<7);
        assert(dr_xenos_draw(v,3,handle,&d)==0);
        const uint32_t factors[]={XSDK_BLEND_SRC_ALPHA,XSDK_BLEND_DST_ALPHA,XSDK_BLEND_CONSTANT_COLOR};
        const uint32_t inverses[]={XSDK_BLEND_ONE_MINUS_SRC_ALPHA,XSDK_BLEND_ONE_MINUS_DST_ALPHA,XSDK_BLEND_ONE_MINUS_CONSTANT_COLOR};
        uint32_t want_src=src==0?XSDK_BLEND_ZERO:src==1 || src==5?XSDK_BLEND_ONE:src==4?inverses[c]:factors[c];
        uint32_t want_dst=dst==0?XSDK_BLEND_ZERO:dst==1 || dst==5?XSDK_BLEND_ONE:dst==4?inverses[c]:factors[c];
        assert(last_blend.color_source==want_src && last_blend.color_dest==want_dst && last_blend.write_mask==5);
        assert(last_blend.color_op==(src==3?XSDK_BLEND_OP_REVERSE_SUBTRACT:dst==3?XSDK_BLEND_OP_SUBTRACT:XSDK_BLEND_OP_ADD));
        assert(material_constants[1]==3 && material_constants[2]==0 && material_constants[3]==8 && material_constants[4]==127);
    }
    unsigned before=draws;
    d.alpha_test=9;assert(dr_xenos_draw(v,3,handle,&d)==DR_XENOS_INVALID);d.alpha_test=8;
    d.blend_key=7u<<1;assert(dr_xenos_draw(v,3,handle,&d)==DR_XENOS_INVALID);d.blend_key=0;
    d.blend_constant=NAN;assert(dr_xenos_draw(v,3,handle,&d)==DR_XENOS_INVALID);
    assert(draws==before);
    assert(dr_xenos_end()==0 && dr_xenos_close()==0);
    puts("PASS: explicit mip-chain preflight/upload, 108 GS blend keys, channel masks and material constant traces (CPU mocks)");
}
