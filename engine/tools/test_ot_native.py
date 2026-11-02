#!/usr/bin/env python3
"""Compare actual native PPC packet structs with the same LE compiler fixture."""
from pathlib import Path
import argparse,re,subprocess,os
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('prefix');p.add_argument('output',type=Path);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
h=(a.upstream/'include/battle/eft_aa.h').read_text();types='\n'.join(re.search(r'typedef struct '+n+r' \{.*?\} '+n+r';',h,re.S)[0] for n in ['EftAaRgbaq','EftAaSt','EftAaXyzf'])
src=out/'fixture.c';src.write_text('#include <stdint.h>\ntypedef uint8_t u8;typedef uint64_t u64;typedef float f32;\n'+types+'''
struct Packet{u64 tag,regs,prim,tex;struct{EftAaRgbaq col;EftAaSt st;EftAaXyzf xyz;} v[4];};
const struct Packet packet __attribute__((section(".fixture"),used))={.tag=0xE400000000008001ull,.regs=0x42142142142160ull,.prim=0x5c,.tex=0x12345678,
.v={{{1,2,3,64,1.0f},{.25f,.75f},{101,202,0x123456,0x80}},{{4,5,6,64,2.0f},{.5f,.25f},{102,203,0x234567,0}},{{7,8,9,64,.5f},{.75f,1.f},{103,204,0x345678,0}},{{10,11,12,64,.25f},{1.f,0.f},{104,205,0x456789,0}}}};
_Static_assert(sizeof(struct Packet)==128,"fixed GIF packet");
''')
for label,cc,oc,flags in [('big',a.prefix+'gcc',a.prefix+'objcopy',['-mcpu=cell']),('little','gcc','objcopy',[])]:
 obj=out/(label+'.o');subprocess.run([cc,*flags,'-std=gnu17','-c',str(src),'-o',str(obj)],check=True);subprocess.run([oc,'-O','binary','--only-section=.fixture',str(obj),str(out/(label+'.bin'))],check=True)
check=out/'check.c';check.write_text('''#include "dr_ot_native.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
int main(int n,char**v){assert(n==3);uint8_t big[128],little[128],copy[128],dst[128],before[128];FILE*f=fopen(v[1],"rb");assert(f&&fread(big,1,128,f)==128);fclose(f);f=fopen(v[2],"rb");assert(f&&fread(little,1,128,f)==128);fclose(f);memcpy(copy,big,128);
assert(!dr_ot_payload(NULL,big,128,1));assert(!dr_ot_payload(dst,big,128,1));for(int i=0;i<128;i+=8)for(int j=0;j<4;j++){uint8_t b=dst[i+j];dst[i+j]=dst[i+7-j];dst[i+7-j]=b;}assert(!memcmp(dst,little,128)&&!memcmp(big,copy,128));assert(!dr_ot_payload(dst,little,128,0)&&!memcmp(dst,little,128));
memset(dst,0xa5,128);memcpy(before,dst,128);assert(dr_ot_payload(dst,big,127,1)<0&&!memcmp(dst,before,128));assert(dr_ot_payload(dst,big,16,1)<0&&!memcmp(dst,before,128));assert(dr_ot_payload(big,big,128,1)<0);assert(dr_ot_payload(dst,NULL,128,1)<0);assert(dr_ot_payload(dst,big,65552,1)<0);assert(dr_ot_payload(dst,big,128,2)<0);
big[15]=3;assert(dr_ot_payload(dst,big,128,1)<0&&!memcmp(dst,before,128));puts("PASS actual PPC/LE compiler struct fixture: RGBA bytes/Q, ST scalars, XYZF bitfields, numeric tags; immutable source, atomic preflight, malformed/unknown/overlap/bounds refusal");}
''')
exe=out/'test';subprocess.run(['gcc','-std=gnu17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+str(root/'src'),str(root/'src/dr_ot_native.c'),str(check),'-o',str(exe)],check=True);r=subprocess.run([str(exe),str(out/'big.bin'),str(out/'little.bin')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'));(out/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
