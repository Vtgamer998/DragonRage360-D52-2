/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "dr_resource.h"
#include "dr_container.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint32_t state=360;
static unsigned next(void) {state=state*1664525u+1013904223u;return state>>16;}
int main(void)
{
    assert(dr_resource_selftest());
    assert(dr_container_selftest());
    for(unsigned test=0;test<10000;test++) {
        unsigned char a[300],before[300],out[256];size_t n=0;
        for(unsigned i=0;i<sizeof(a);i++) a[i]=(unsigned char)next();
        size_t length=next()%sizeof(a);
        memcpy(before,a,sizeof(a));
        int e=dr_anim_prepare(a,length,1);
        if(e) assert(!memcmp(a,before,sizeof(a)));
        size_t nodes=0,offset=0,span=0;
        memcpy(a,before,sizeof(a));
        e=dr_skeleton_prepare(a,length,1,&nodes);
        if(e) assert(!memcmp(a,before,sizeof(a)));
        dr_pack_memory_entry(before,length,next()%4096,&offset,&span,NULL);
        /* plausible small BPE header, hostile dictionary/payload */
        a[0]=(unsigned char)next();a[1]=a[2]=a[3]=0;
        a[4]=(unsigned char)next();a[5]=a[6]=a[7]=0;
        dr_bpe_decode(a,length,out,sizeof(out),&n);assert(n<=sizeof(out));
    }
    puts("PASS: resource self-test and 10000 hostile buffers under ASan/UBSan");
}
