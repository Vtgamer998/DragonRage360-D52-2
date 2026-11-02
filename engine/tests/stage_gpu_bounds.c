/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "../../src/dr_vu1.c"
#include <assert.h>
static int calls,refuse;
static int renderer(void *o,const uint8_t *env,uint32_t prim,const float *v,uint32_t n,const float *m){
 (void)o;assert(env==vu.mem+141*16 && (prim&7)==4 && n==3);assert(v[0]==1.25f && m[0]==1 && m[15]==1);calls++;return refuse?-77:0;
}
static void w32(void *v,uint32_t n){uint8_t*p=v;for(int i=0;i<4;i++)p[i]=(uint8_t)(n>>(i*8));}
static void w64(void *v,uint64_t n){w32(v,(uint32_t)n);w32((uint8_t*)v+4,(uint32_t)(n>>32));}
static void seed(void){
 dr_vu1_reset(NULL,NULL);dr_vu1_stage_renderer(renderer,NULL);vu.top=141;
 for(unsigned i=0;i<16;i++)w32(vu.mem+i*4,i%5==0?0x3f800000:0);
 uint8_t*p=vu.mem+141*16;w64(p,0x1000000000008001ull);w32(p+8,14);w32(p+24,6);
 w64(p+32,3ull|(1ull<<46)|(4ull<<47)|(3ull<<60));w32(p+40,0x512);w32(p+48,0x3fa00000);
}
int main(int argc,char**argv){assert(argc==2);unsigned char program[3192];FILE*f=fopen(argv[1],"rb");assert(f && fread(program,1,sizeof(program),f)==sizeof(program));fclose(f);
 seed();assert(!dr_vu1_program(0,program,sizeof(program)));assert(stage_program());vu.micro[17]^=1;stage_dirty=1;assert(!stage_program());
 seed();assert(stage_batch()==1&&calls==1);uint8_t*p=vu.mem+141*16;w32(p+8,0x3f);w32(p+40,0x412);assert(stage_batch()==1&&calls==2);
 for(unsigned k=0;k<10;k++){seed();p=vu.mem+141*16;switch(k){
 case 0:vu.top=1023;break;case 1:w64(p+32,2ull|(4ull<<47)|(3ull<<60));break;
 case 2:w64(p+32,257ull|(4ull<<47)|(3ull<<60));break;
 case 3:w32(p+8,2);break;case 4:w32(p+24,5);break;case 5:w32(p+28,1);break;
 case 6:w32(p+40,0x123);break;case 7:w32(p+44,1);break;case 8:w32(vu.mem,0x7fc00000);break;case 9:w32(p+48,0x7f800000);break;
 }assert(!stage_batch());}
 assert(calls==2);seed();refuse=1;assert(stage_batch()==-77);assert(dr_vu1_call(-1,0)==DR_VU_INVALID);dr_vu1_stage_renderer(NULL,NULL);assert(!stage_renderer);puts("PASS: actual stage classifier, endian floats, A+D/NOP and XYZ2/XYZF2; ten bad layouts; renderer errors and zero budget refused");return 0;
}
