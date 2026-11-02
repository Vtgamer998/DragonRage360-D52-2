#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
from pathlib import Path
import re,subprocess,os,argparse
a=argparse.ArgumentParser();a.add_argument('upstream',type=Path);a.add_argument('prefix');a.add_argument('output',type=Path);a=a.parse_args();r=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
s=(a.upstream/'src/battle/eft_ae.c').read_text();names=['EftAePktHead','EftAeXyzf','EftAeGsVtx','EftAeTriPkt'];types='\n'.join(re.search(r'typedef struct '+n+r' \{.*?\} '+n+r';',s,re.S)[0] for n in names)
source='#include <stdint.h>\ntypedef uint8_t u8;typedef uint32_t u32;typedef uint64_t u64;typedef float f32;\n'+types+'\n'
for ctx in range(2):
 source+=f'const EftAeTriPkt packet{ctx} __attribute__((section(".fixture{ctx}"),used))={{.h={{.tag=0x20000008,.vif0=0x10000000,.vif1=0x50000008,.gif0=0xE400000000008001ull,.gif1=0xF8421421421860ull+{ctx}*(0x1000000000110ull)}},.prim=0x5b,.tex0=0x12345678,.clamp=5,.v={{{{.rgba={{1,2,3,128}},.q=1,.s=.25,.t=.75,.xyz={{101,202,0x123456,0x80}}}},{{.rgba={{4,5,6,64}},.q=.5,.s=.5,.t=.25,.xyz={{102,203,0x234567,0}}}},{{.rgba={{7,8,9,32}},.q=2,.s=.75,.t=1,.xyz={{103,204,0x345678,0}}}}}}}};\n'
source+='_Static_assert(sizeof(EftAeTriPkt)==144,"native triangle packet");\n';(out/'fixture.c').write_text(source)
prefix=str(Path(a.prefix).resolve())
for label,cc,oc,flags in [('big',prefix+'gcc',prefix+'objcopy',['-mcpu=cell']),('little','gcc','objcopy',[])]:
 obj=out/(label+'.o');subprocess.run([cc,*flags,'-std=gnu17','-c',str(out/'fixture.c'),'-o',str(obj)],check=True)
 for ctx in range(2):subprocess.run([oc,'-O','binary','--only-section=.fixture'+str(ctx),str(obj),str(out/f'{label}{ctx}.bin')],check=True)
check='''#include "dr_ot_native.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
int main(int n,char **v){assert(n==5);for(int k=0;k<2;k++){uint8_t b[144],l[144],d[128],original[144];FILE*f=fopen(v[1+k*2],"rb");assert(f && fread(b,1,144,f)==144);fclose(f);f=fopen(v[2+k*2],"rb");assert(f && fread(l,1,144,f)==144);fclose(f);memcpy(original,b,144);assert(!dr_ot_payload(d,b+16,128,1));for(int i=0;i<128;i+=8)for(int j=0;j<4;j++){uint8_t x=d[i+j];d[i+j]=d[i+7-j];d[i+7-j]=x;}assert(!memcmp(d,l+16,128) && !memcmp(b,original,144));}puts("PASS original EftSprAnim_QueueTri context 0/1: numeric CLAMP plus byte RGBAQ, scalar ST, PPC XYZF; source immutable");}'''
(out/'test.c').write_text(check)
subprocess.run(['gcc','-std=gnu17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+str(r/'src'),str(r/'src/dr_ot_native.c'),str(out/'test.c'),'-o',str(out/'test')],check=True)
subprocess.run([str(out/'test'),str(out/'big0.bin'),str(out/'little0.bin'),str(out/'big1.bin'),str(out/'little1.bin')],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
