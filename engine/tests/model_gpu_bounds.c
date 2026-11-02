/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "../../src/dr_vu1.c"
#include <assert.h>
static int calls,refuse;
static int renderer(void *o,const uint8_t *env,uint32_t prim,const float *v,uint32_t n,const float *c,unsigned layer){
 (void)o;assert((prim&7)==4 && n==3 && (layer==0 || layer==1 || layer==5));
 assert(v[0]==1.25f && c[0]==1 && c[15]==1 && v[7]==0 && v[11]==0);assert(stage_u64(env+8)==14);calls++;return refuse?-77:0;
}
static void w32(void *p,uint32_t n){vu_store(p,n);}
static void w64(void *v,uint64_t n){w32(v,(uint32_t)n);w32((uint8_t*)v+4,(uint32_t)(n>>32));}
static void seed(void){
 dr_vu1_reset(NULL,NULL);dr_vu1_model_renderer(renderer,NULL);vu.top=34;
 for(unsigned i=0;i<16;i++)w32(vu.mem+i*4,i%5==0?0x3f800000:0);
 uint8_t *p=vu.mem+34*16;w64(p,0x1000000000000001ull);w64(p+8,14);
 w64(p+24,6);w64(p+40,7);w64(vu.mem+26*16+8,7);
 for(unsigned l=0;l<2;l++){w64(p+48+l*16,3ull|(1ull<<46)|((uint64_t)(0x5c+l*512)<<47)|(3ull<<60));w64(p+56+l*16,0x512);}
 w32(p+80,0x3fa00000);w32(p+108,1);w32(p+124,0x7fc00000); // padding is not a float operand
}
int main(int argc,char**argv){assert(argc==3);
 for(int i=0;i<2;i++){seed();FILE *f=fopen(argv[i+1],"rb");assert(f);uint8_t b[1016];size_t n=fread(b,1,sizeof(b),f);fclose(f);assert(!dr_vu1_program(0,b,n));assert(model_program()==i+1);vu.micro[17]^=1;model_dirty=1;assert(!model_program());}
 seed();uint8_t original[0x4000];memcpy(original,vu.mem,sizeof(original));assert(model_batch(1)==1 && calls==2);assert(!memcmp(original,vu.mem,sizeof(original)));
 seed();assert(model_batch(2)==1 && calls==4);
 seed();w32(vu.mem+34*16+108,0);assert(model_batch(2)==1 && calls==4);
 for(unsigned k=0;k<14;k++){seed();uint8_t*p=vu.mem+34*16;switch(k){
 case 0:vu.top=1023;break;case 1:w64(p+48,2ull|(4ull<<47)|(3ull<<60));break;
 case 2:w64(p+48,257ull|(4ull<<47)|(3ull<<60));break;case 3:w64(p+8,15);break;
 case 4:w64(p+24,5);break;case 5:w32(p+28,1);break;case 6:w64(p+56,0x123);break;
 case 7:w64(p+72,0x123);break;case 8:w32(vu.mem,0x7fc00000);break;case 9:w32(p+80,0x7f800000);break;
 case 10:w32(p+112,0x7f800000);break;case 11:w64(p+40,9);break;
 case 12:w64(p+64,4ull|(4ull<<47)|(3ull<<60));break;case 13:w64(p+48,3ull|(0x7cull<<47)|(3ull<<60));break;
 }assert(!model_batch(1));}assert(calls==4);
 seed();refuse=1;assert(model_batch(1)==-77 && calls==5);
 /* Same known microcode and strip, reference VM versus shader handoff.
  * This compares interpreted instruction counts, never console frame rate. */
 uint32_t instructions[2];
 for(unsigned accelerated=0;accelerated<2;accelerated++){
  seed();refuse=0;calls=0;
  for(unsigned mat=0;mat<4;mat++){
   unsigned offset=mat==0?0:mat==1?16:mat==2?56:72;
   for(unsigned i=0;i<16;i++)w32(vu.mem+(offset+i)*4,i%5==0?0x3f800000:0);
  }
  uint8_t *packet=vu.mem+34*16;w64(packet+64,stage_u64(packet+64)|(1ull<<15));
  FILE *f=fopen(argv[1],"rb");assert(f);uint8_t b[1016];assert(fread(b,1,sizeof(b),f)==sizeof(b));fclose(f);
  assert(!dr_vu1_program(0,b,sizeof(b)) && !dr_vu1_address(34,0,0));
  if(!accelerated)dr_vu1_model_renderer(NULL,NULL);
  assert(!dr_vu1_call(0,250000));
  int rc=dr_vu1_call(-1,250000);if(rc)fprintf(stderr,"reference comparison rc=%d pc=%u kick=%d\n",rc,vu.pc,kick_error);
  assert(!rc);instructions[accelerated]=sStatInstr;
  if(accelerated)assert(calls==2 && model_draws==2);
 }
 assert(instructions[1]<instructions[0]);
 printf("PASS same synthetic strip: reference VM instructions=%u, shader-path VM instructions=%u; NOT a GPU/FPS benchmark\n",instructions[0],instructions[1]);
 dr_vu1_model_renderer(NULL,NULL);assert(!model_renderer);
 puts("PASS model bridge: two real source-assembled microcode identities, single-byte mutation refusal, two-layer atomic preflight, LE floats, immutable source, 14 malformed layouts, marker skip, error propagation");return 0;
}
