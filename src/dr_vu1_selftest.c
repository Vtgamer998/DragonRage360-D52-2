/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Synthetic two-bone matrix/weight microprogram for the original interpreter.
 * No commercial VU microcode or game resource is included.
 */
#include "dr_vu1.h"
#include <string.h>
static void word(unsigned char *p,uint32_t v) {for(unsigned i=0;i<4;i++) p[i]=(unsigned char)(v>>(i*8));}
static void instruction(unsigned char *code,unsigned *n,uint32_t lower,uint32_t upper)
{word(code+(*n)*8,lower);word(code+(*n)*8+4,upper);(*n)++;}
static uint32_t upper(unsigned op,unsigned fd,unsigned fs,unsigned ft)
{return 15u<<21|ft<<16|fs<<11|fd<<6|op;}
static uint32_t special(unsigned op,unsigned fs,unsigned ft)
{return upper(0x3c|(op&3),op>>2,fs,ft);}
int dr_vu1_selftest(void)
{
    unsigned char code[256]={0},data[160]={0},zero[16]={0},result[16];unsigned n=0;
    const float vectors[10][4]={
        {2,4,6,1},{1,0,0,0},{0,1,0,0},{0,0,1,0},{10,0,0,1},
        {2,0,0,0},{0,3,0,0},{0,0,4,0},{0,20,0,1},{.25f,.75f,0,0}
    };
    for(unsigned i=0;i<10;i++) for(unsigned j=0;j<4;j++) {
        uint32_t bits;memcpy(&bits,&vectors[i][j],4);word(data+i*16+j*4,bits);
    }
    for(unsigned i=0;i<10;i++) instruction(code,&n,15u<<21|(i+1)<<16|i,0x2ff); /* LQ */
    for(unsigned bone=0;bone<2;bone++) {
        unsigned base=2+bone*4;
        instruction(code,&n,0x8000033c,special(0x18,base,1)); /* MULA x */
        instruction(code,&n,0x8000033c,special(0x09,base+1,1)); /* MADDA y */
        instruction(code,&n,0x8000033c,special(0x0a,base+2,1)); /* MADDA z */
        instruction(code,&n,0x8000033c,upper(0x0b,11+bone,base+3,1)); /* MADD w */
    }
    instruction(code,&n,0x8000033c,special(0x18,11,10));
    instruction(code,&n,0x8000033c,upper(0x09,13,12,10));
    instruction(code,&n,1u<<25|15u<<21|13u<<11|10u,0x2ff); /* SQ qword 10 */
    instruction(code,&n,0x8000033c,0x400002ff);instruction(code,&n,0x8000033c,0x2ff);
    dr_vu1_reset(NULL,NULL);
    if(dr_vu1_control(1,1,0,0,zero,zero) || dr_vu1_program(0,code,n*8) ||
       dr_vu1_unpack(0x6c,10,0,data,sizeof(data)) || dr_vu1_call(0,64) || dr_vu1_read(160,result,sizeof(result))) return 0;
    const float expected[4]={6,25,19.5f,1};
    for(unsigned i=0;i<4;i++) {
        uint32_t bits=(uint32_t)result[i*4]|(uint32_t)result[i*4+1]<<8|(uint32_t)result[i*4+2]<<16|(uint32_t)result[i*4+3]<<24;
        float value;memcpy(&value,&bits,4);if(value!=expected[i]) return 0;
    }
    return 1;
}
