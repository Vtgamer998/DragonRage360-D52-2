/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Reuses GPU call/lifetime mocks. No rasterization or Xbox emulation. */
#define main previous_lifetime_tests
#include "test_xenos.c"
#undef main
#include "dr_gs_replay.h"
#include <stdlib.h>
static unsigned resolutions;
static uint32_t resolve(GsTex tex,void *opaque)
{assert(opaque==&resolutions);resolutions++;return tex==123?1:0;}
int main(void)
{
    previous_lifetime_tests();
    assert(dr_xenos_open()==0);
    uint32_t texture;const unsigned char rgba[]={255,255,255,128};
    assert(dr_xenos_texture_create(1,1,rgba,&texture)==0 && texture==1);
    uint32_t keys[]={15u<<16|1u<<10,15u<<16|1u<<10|69u};
    DrGsReplay r={.physical_width=640,.physical_height=480,.logical_width=640,.logical_height=480,
        .target=2,.white_texture=1,.pipeline_keys=keys,.pipeline_count=2,.texture=resolve,.opaque=&resolutions};
    Vtx v[6]={{0,0,.5f,128,128,128,128,0,0,1},{32,0,.5f,128,128,128,128,1,0,1},
        {0,32,.5f,128,128,128,128,0,1,1}};
    memcpy(v+3,v,3*sizeof(*v));
    GsDraw d[2]={{.first=0,.count=3,.target=2,.scissor={0,0,640,480}},
        {.first=3,.count=3,.target=2,.tex=123,.mode={1,0,1,2},.pipeline=1,.scissor={0,0,640,480}}};
    uint32_t failed;unsigned before=draws;
    assert(dr_gs_replay(&r,d,2,v,6,&failed)==0 && draws==before+2 && failed==UINT32_MAX && resolutions==1);
    assert(vertex_address==0x100000+144 && vertex_bytes==144);
    GsDraw saved=d[1];
    d[1].misc[3]=-1;d[1].rect[0]=d[1].rect[1]=0;d[1].rect[2]=d[1].rect[3]=1;
    assert(dr_gs_replay(&r,d,2,v,6,&failed)==0);
    d[1].rect[0]=-1;d[1].rect[1]=-1;d[1].rect[2]=2;d[1].rect[3]=2;
    assert(dr_gs_replay(&r,d,2,v,6,&failed)==0);
    d[1].rect[0]=-4097;before=draws;
    assert(dr_gs_replay(&r,d,2,v,6,&failed)==DR_XENOS_INVALID && draws==before);
    d[1].rect[0]=NAN;before=draws;
    assert(dr_gs_replay(&r,d,2,v,6,&failed)==DR_XENOS_INVALID && draws==before);
    d[1]=saved;d[1].misc[3]=1;
    assert(dr_gs_replay(&r,d,2,v,6,&failed)==0); /* resolved white: constant sampling */
    r.white_texture=2;before=draws;
    assert(dr_gs_replay(&r,d,2,v,6,&failed)==DR_GS_UNSUPPORTED && draws==before);
    r.white_texture=1;d[1]=saved;

#define REJECT(change,code) do {unsigned b=draws;d[1]=saved;change;assert(dr_gs_replay(&r,d,2,v,6,&failed)==code);assert(draws==b && failed==1);} while(0)
    REJECT(d[1].vu=4,DR_GS_UNSUPPORTED);
    REJECT(d[1].native=1,DR_GS_UNSUPPORTED);
    REJECT(d[1].target=3,DR_GS_UNSUPPORTED);
    REJECT(d[1].tex_is_target=1,DR_GS_UNSUPPORTED);
    REJECT(d[1].sampler=16,DR_GS_UNSUPPORTED);
    REJECT(d[1].mode[1]=4,DR_GS_UNSUPPORTED);
    REJECT(d[1].mode[2]=2,DR_GS_UNSUPPORTED);
    REJECT(d[1].mode[3]=9,DR_GS_UNSUPPORTED);
    REJECT(d[1].misc[1]=1,DR_GS_UNSUPPORTED); /* DATE */
    REJECT(d[1].misc[2]=1,DR_GS_UNSUPPORTED); /* FBA */
    REJECT(d[1].misc[3]=2,DR_GS_UNSUPPORTED);
    REJECT(d[1].tex=999,DR_GS_TEXTURE);
    REJECT(d[1].pipeline=-1,DR_XENOS_INVALID);
    REJECT(d[1].pipeline=2,DR_XENOS_INVALID);
    REJECT(d[1].first=UINT32_MAX,DR_XENOS_INVALID);
    REJECT(d[1].count=UINT32_MAX,DR_XENOS_INVALID);
    REJECT(d[1].count=0,DR_XENOS_INVALID);
    REJECT(d[1].count=1024,DR_XENOS_INVALID); /* range validation precedes capacity */
    d[1]=saved;
    /* Pipeline semantics: none of these can be silently approximated. */
    uint32_t original=keys[1];
    const uint32_t bad[]={1u<<15,1u<<20,1u<<22,1u<<24,1u<<31};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        keys[1]=original|bad[i];before=draws;
        assert(dr_gs_replay(&r,d,2,v,6,&failed)==DR_GS_UNSUPPORTED && failed==1 && draws==before);
    }
    keys[1]=(original&~1023u)|533u; /* Source-alpha shader scaling before additive blending. */
    before=draws;assert(dr_gs_replay(&r,d,2,v,6,&failed)==0 && draws==before+2);
    assert(material_constants[5]==1 && last_blend.color_source==XSDK_BLEND_ONE);
    keys[1]=original;
    for(int tfx=0;tfx<4;tfx++) for(int tcc=0;tcc<2;tcc++) for(int atst=0;atst<=8;atst++) {
        d[1]=saved;d[1].mode[1]=tfx;d[1].mode[2]=tcc;d[1].mode[3]=atst;d[1].misc[0]=127;
        assert(dr_gs_replay(&r,d,2,v,6,&failed)==0);
        assert(material_constants[0]==1 && material_constants[1]==tfx && material_constants[2]==tcc &&
            material_constants[3]==atst && material_constants[4]==127);
    }
    d[1]=saved;keys[1]=(original&~(15u<<16))|(5u<<16);
    assert(dr_gs_replay(&r,d,2,v,6,&failed)==0 && last_blend.write_mask==5);
    keys[1]=original;
    REJECT(d[1].misc[0]=NAN,DR_XENOS_INVALID);
    REJECT(d[1].misc[0]=127.5f,DR_XENOS_INVALID);
    d[1]=saved;before=draws;
    /* Invalid data in the LAST draw must not emit the first. */
    v[5].q=0;before=draws;
    assert(dr_gs_replay(&r,d,2,v,6,&failed)==DR_XENOS_INVALID && draws==before && failed==UINT32_MAX);
    v[5].q=1;v[5].x=NAN;
    assert(dr_gs_replay(&r,d,2,v,6,&failed)==DR_XENOS_INVALID && draws==before);v[5].x=0;
    d[1].mode[0]=0;d[1].tex=999;unsigned old_resolutions=resolutions;
    assert(dr_gs_replay(&r,d,2,v,6,&failed)==0 && resolutions==old_resolutions);
    d[1]=saved;r.white_texture=15; /* Invalid native handle, even for the first draw. */
    before=draws;assert(dr_gs_replay(&r,d,2,v,6,&failed)==DR_XENOS_INVALID && draws==before);
    r.white_texture=1;
    /* A mapped handle can also fail late: preflight still prevents partial frames. */
    r.white_texture=1;d[0]=saved;d[0].first=0;d[1].mode[0]=0;r.white_texture=15;
    assert(dr_gs_replay(&r,d,2,v,6,&failed)==DR_XENOS_INVALID && draws==before);
    r.white_texture=1;d[0]=saved;d[0].first=0;d[1]=saved;
    Vtx many[DR_XENOS_VERTEX_CAPACITY-1];
    for(unsigned i=0;i<DR_XENOS_VERTEX_CAPACITY-1;i++)many[i]=v[i%3];
    d[0].first=0;d[0].count=180;d[1].first=0;d[1].count=6;
    /* A valid real loading frame needs 186 vertices. Keep the hard limit. */
    assert(dr_gs_replay(&r,d,2,many,DR_XENOS_VERTEX_CAPACITY-1,&failed)==0);
    before=draws;d[0].count=DR_XENOS_VERTEX_CAPACITY-1;d[1].count=3;
    assert(dr_gs_replay(&r,d,2,many,DR_XENOS_VERTEX_CAPACITY-1,&failed)==DR_XENOS_FULL && failed==1 && draws==before);
    assert(dr_gs_replay(&r,d,DR_XENOS_VERTEX_CAPACITY+1,many,DR_XENOS_VERTEX_CAPACITY-1,&failed)==DR_XENOS_FULL && failed==UINT32_MAX);
    assert(dr_gs_replay(NULL,d,2,v,6,&failed)==DR_XENOS_INVALID);
    assert(dr_xenos_begin(640,480)==0);
    d[0]=saved;d[0].first=0;d[1]=saved;
    assert(dr_gs_replay(&r,d,2,v,6,&failed)==DR_XENOS_STATE && draws==before);
    /* The caller's active batch remains active after a rejected nested begin. */
    assert(dr_xenos_end()==0);
    DrGsVertex nv[3];memcpy(nv,v,3*sizeof(*nv));
    DrXenosCommand commands[2]={{.vertices=nv,.count=3,.texture=1,
        .draw={.logical_width=640,.logical_height=480,.depth=1,.scissor={0,0,640,480}}}};
    commands[1]=commands[0];commands[1].texture=99;
    assert(dr_xenos_begin(640,480)==0);
    assert(dr_xenos_draw_list(commands,2)==DR_XENOS_INVALID && draws==before);
    commands[1].texture=1;assert(dr_xenos_draw_list(commands,2)==0 && draws==before+2);
    assert(dr_xenos_end()==0);
    assert(dr_xenos_target_create(0,640,480)==0 && dr_xenos_target_create(1,640,480)==0);
    { /* Consecutive simple GS draws merge, while late bad vertices still reject the whole list. */
        GsDraw simple[3];Vtx triangles[9];DrXenosProfile xp0,xp1;
        assert(dr_xenos_target_create(2,640,480)==0);
        for(unsigned i=0;i<9;i++)triangles[i]=v[i%3];
        for(unsigned i=0;i<3;i++)simple[i]=(GsDraw){.first=i*3,.count=3,.target=2,.scissor={0,0,640,480}};
        r.saved_targets=1;before=draws;dr_xenos_profile(&xp0);
        assert(dr_gs_replay(&r,simple,3,triangles,9,&failed)==0 && draws==before+1);
        dr_xenos_profile(&xp1);assert(xp1.validated_vertices-xp0.validated_vertices==9 && xp1.reused_validation_vertices-xp0.reused_validation_vertices==9);
        DrGsReplayProfile batch;dr_gs_replay_profile(&batch);assert(batch.merged_draws==2 && batch.replay_commands==1);
        before=draws;triangles[8].q=0;
        assert(dr_gs_replay(&r,simple,3,triangles,9,&failed)==DR_XENOS_INVALID && draws==before);
        triangles[8].q=1;simple[1].pipeline=1;dr_xenos_profile(&xp0);
        assert(dr_gs_replay(&r,simple,3,triangles,9,&failed)==0);
        dr_xenos_profile(&xp1);assert(xp1.draws-xp0.draws==3);
        assert(dr_xenos_target_drop(2)==0);
    }
    r.saved_targets=1;r.white_texture=1;d[0]=saved;d[0].first=0;d[0].target=0;d[1]=saved;d[1].target=1;
    { /* Several draw calls share the frame arena, without a vertex spill. */
        Vtx *frame=malloc(2046*sizeof(*frame));assert(frame);for(unsigned j=0;j<2046;j++)frame[j]=v[j%3];
        GsDraw large[2]={d[0],d[1]};large[0].count=2046;large[1].first=0;large[1].count=3;
        unsigned oldwaits=waits;before=draws;
        assert(dr_gs_replay(&r,large,2,frame,2046,&failed)==0 && draws==before+3 && waits<=oldwaits+1);
        before=draws;frame[2045].x=NAN;
        assert(dr_gs_replay(&r,large,2,frame,2046,&failed)==DR_XENOS_INVALID && draws==before);
        free(frame);
    }

    model_enabled=1;next_color=0x12345678;next_depth=.5f;
    assert(dr_gs_replay(&r,d,2,v,6,&failed)==0);
    assert(dr_xenos_begin_target(0)==0 && edram_color==0x12345678 && edram_depth==.5f);
    assert(dr_xenos_end()==0);
    before=draws;d[1].target=23;
    assert(dr_gs_replay(&r,d,2,v,6,&failed)==DR_XENOS_INVALID && draws==before);
    d[1].target=1;v[5].q=0;
    assert(dr_gs_replay(&r,d,2,v,6,&failed)==DR_XENOS_INVALID && draws==before);v[5].q=1;
    d[1].misc[1]=1;
    assert(dr_gs_replay(&r,d,2,v,6,&failed)==DR_GS_UNSUPPORTED && draws==before);
    model_enabled=0;
    assert(dr_xenos_close()==0);
    puts("PASS: actual GsDraw ABI, pipeline translation, whole-list preflight, unsupported-state rejection; CPU mocks only");
}
