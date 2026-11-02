/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "dr_vu1.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint32_t seed=360;
static unsigned next(void) {seed=seed*1664525u+1013904223u;return seed>>16;}
int main(void)
{
    unsigned char input[4096],row[16]={0},col[16]={0},memory[16384],before[16384];
    assert(dr_vu1_selftest());
    assert(dr_vu1_control(0,1,0,0,row,col)==DR_VU_INVALID);
    for(unsigned test=0;test<10000;test++) {
        dr_vu1_reset(NULL,NULL);
        assert(!dr_vu1_control(1+next()%8,1+next()%8,next()%3,next(),row,col));
        for(unsigned i=0;i<sizeof(input);i++) input[i]=(unsigned char)next();
        unsigned cmd=0x60|(next()%32),num=next()%257,imm=next()%65536,bytes=next()%4096;
        assert(!dr_vu1_read(0,before,sizeof(before)));
        int status=dr_vu1_unpack(cmd,num,imm,input,bytes);
        assert(!dr_vu1_read(0,memory,sizeof(memory)));
        if(status) assert(!memcmp(before,memory,sizeof(before)));
    }
    puts("PASS: original VU interpreter two-bone microprogram and 10000 checked UNPACK cases under ASan/UBSan");
}
