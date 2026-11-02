/* SPDX-License-Identifier: AGPL-3.0-or-later */
#define main old_model_tests
#include "model_gpu_bounds.c"
#undef main
static unsigned shadow_calls;
static int shadow_renderer(void *o,const uint8_t *env,uint32_t prim,const float *v,uint32_t n,const float *c,unsigned layer){
 (void)o;(void)env;assert((prim&7)==4 && n==3 && layer==2);
 assert(v[0]==.125f && v[4]==0 && v[7]==0 && v[11]==0);
 assert(c[0]==1 && c[15]==1 && c[56]==1 && c[71]==1 && c[88]==128 && c[91]==128);
 shadow_calls++;return 0;
}
static void shadow_seed(void){
 seed();dr_vu1_model_renderer(shadow_renderer,NULL);w64(vu.mem+34*16+64,stage_u64(vu.mem+34*16+64)|(1ull<<15));
 for(unsigned mat=0;mat<4;mat++){
  unsigned off=mat==0?0:mat==1?16:mat==2?44:60;
  for(unsigned i=0;i<16;i++)w32(vu.mem+(off+i)*4,i%5==0?0x3f800000:0);
 }
 for(unsigned i=0;i<4;i++)w32(vu.mem+(40+i)*4,0x43000000);
 for(unsigned i=0;i<3;i++){
  uint8_t *v=vu.mem+34*16+80+i*48;w32(v,0x3e000000);w32(v+12,0x3f800000);
  for(unsigned j=4;j<=7;j++)w32(v+j*4,0x7fc00000);
 }
}
int main(int argc,char **argv){assert(argc==3);
 for(unsigned kind=3;kind<=4;kind++){
  uint8_t code[808];FILE*f=fopen(argv[kind-2],"rb");assert(f);size_t bytes=fread(code,1,sizeof(code),f);fclose(f);
  uint32_t instructions[2];
  for(unsigned hle=0;hle<2;hle++){
   shadow_seed();if(!hle)dr_vu1_model_renderer(NULL,NULL);
   assert(!dr_vu1_program(0,code,bytes));assert(model_program()==(int)kind);
   vu.micro[1]^=1;model_dirty=1;assert(!model_program());vu.micro[1]^=1;model_dirty=1;
   assert(!dr_vu1_address(34,0,0));assert(!dr_vu1_call(0,250000));
   uint8_t before[0x4000];memcpy(before,vu.mem,sizeof(before));
   int rc=dr_vu1_call(-1,250000);if(rc)fprintf(stderr,"shadow kind=%u hle=%u rc=%d pc=%u kick=%d\n",kind,hle,rc,vu.pc,kick_error);assert(!rc);instructions[hle]=sStatInstr;
   if(hle)assert(!memcmp(before,vu.mem,sizeof(before)));
  }
  assert(instructions[1]<instructions[0]);printf("PASS shadow %u reference=%u shader=%u VM instructions\n",kind,instructions[0],instructions[1]);
  shadow_seed();vu.top=1023;assert(!model_shadow_batch(kind));
  shadow_seed();w64(vu.mem+34*16+64,81ull|(4ull<<47)|(3ull<<60));assert(!model_shadow_batch(kind));
  shadow_seed();w32(vu.mem,0x7fc00000);assert(!model_shadow_batch(kind));
 }
 assert(shadow_calls==2);puts("PASS exact shadow identities, mutation refusal, constant remap, ignored normal words, immutable inputs and bounds");
}
