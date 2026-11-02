/* SPDX-License-Identifier: AGPL-3.0-or-later */
#define main earlier_native_tests
#include "test_xenos.c"
#undef main
#include "dr_targets.h"
int main(void){
 assert(!dr_xenos_open());uint32_t tex;uint8_t white[]={255,255,255,128};assert(!dr_xenos_texture_create(1,1,white,&tex));assert(!dr_xenos_target_create_gs(0,64,64));
 static DrGsVertex v[1024];for(unsigned i=0;i<1024;i++)v[i]=(DrGsVertex){(float)(i%32),(float)(i/32),.5f,128,128,128,128,0,0,1};
 static DrXenosTargetCommand c[257];
 DrXenosDraw d={.logical_width=64,.logical_height=64,.topology=2,.depth=1,.scissor={0,0,64,64}};
 for(unsigned i=0;i<257;i++)c[i]=(DrXenosTargetCommand){.target=0,.command={.vertices=v,.count=1024,.texture=tex,.draw=d}};
 unsigned passes=tile_passes;assert(dr_xenos_replay_targets(c,257)==DR_XENOS_FULL && tile_passes==passes);
 assert(!dr_xenos_replay_targets(c,256));DrXenosProfile xp;DrTargetProfile tp;dr_xenos_profile(&xp);dr_targets_profile(&tp);
 assert(xp.draws==256 && !xp.arena_restarts && tp.loads==1 && tp.stores==1);
 assert(vertex_address==0x100000+(DR_XENOS_FRAME_VERTICES-1024)*48);
 /* The first fetch is still intact at the end of a full 12 MiB frame. */
 float first;memcpy(&first,arena+2,4);assert(first==.5f);
 assert(!dr_xenos_begin_target(0));dr_targets_profile(&tp);assert(tp.zero_stencil_restores==1 && !tp.stencil_restore_passes && !stencil_restores);assert(!dr_xenos_end());
 unsigned frees=freed;wait_result=STATUS_IO_TIMEOUT;assert(dr_xenos_close()==DR_XENOS_GPU && freed==frees);
 wait_result=STATUS_SUCCESS;assert(!dr_xenos_close());
 puts("PASS frame arena: exact 262144 vertices, overflow refused before GPU, 256 draws/one tile pass, immutable first fetch, timeout retains storage");return 0;
}
