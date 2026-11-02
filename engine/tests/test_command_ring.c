/* SPDX-License-Identifier: AGPL-3.0-or-later */
#define main old_native_tests
#include "test_xenos.c"
#undef main
#include "dr_targets.h"
int main(void){assert(!dr_targets_open());mock_command_failure=3;assert(dr_targets_ensure_gs(0,64,64)==DR_XENOS_GPU && mock_command_frees==2);for(unsigned i=0;i<32;i++)assert(!mock_command_live[i]);mock_command_failure=0;assert(!dr_targets_ensure_gs(0,64,64));
 const XsdkCommandBuffer *seen[8];unsigned oldwaits=waits;
 for(unsigned i=0;i<8;i++){assert(!dr_targets_bind(0));seen[i]=mock_active_command;for(unsigned j=0;j<i;j++)assert(seen[i]!=seen[j]);assert(!dr_targets_store());}
 assert(waits==oldwaits);unsigned frees=mock_command_frees;wait_result=STATUS_IO_TIMEOUT;
 assert(dr_targets_bind(0)==DR_XENOS_GPU && !tile_recording && mock_command_frees==frees);
 assert(dr_targets_close()==DR_XENOS_GPU && mock_command_frees==frees);
 wait_result=STATUS_SUCCESS;assert(!dr_targets_bind(0) && mock_active_command==seen[0]);assert(!dr_targets_store());assert(!dr_targets_close() && mock_command_frees==frees+8);
 puts("PASS immutable command ring: eight unique in-flight buffers, wait only on reuse, timeout retains all buffers, safe retry and fenced close");}
