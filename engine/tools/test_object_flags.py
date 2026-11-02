#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
import argparse,subprocess,os
from pathlib import Path
from fixed_profile_source import transform
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('output',type=Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
s,_=transform('src/battle/bobj_a.c',(a.upstream/'src/battle/bobj_a.c').read_text());start=s.index('s32 BtlObj_IsPartVisible(');end=s.index('/* Returns the bounds record',start);code=s[start:end];assert 'FLAG_BITS(' not in code
code='''#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef int s32;typedef uint32_t u32;typedef struct {struct {u32 flags;} state;} BObj;
'''+code+'''
int main(void){const u32 bits[5]={2,16,32,64,128};BObj obj;
for(u32 f=0;f<1024;f++){obj.state.flags=f;for(u32 p=0;p<8;p++){int expected=p==0?!!(f&2):p<5?!!((f&2)&&(f&bits[p])):0;assert(BtlObj_IsPartVisible(&obj,p)==expected);}}
for(u32 b=0;b<32;b++){obj.state.flags=(1u<<b);assert(BtlObj_IsPartVisible(&obj,0)==(b==1));}
puts("PASS original BtlObj_IsPartVisible after numeric flag adaptation: 8192 part/flag cases plus 32 one-hot words; host bitfield ordering not used");return 0;}
'''
f=a.output/'object-flags.c';f.write_text(code)
subprocess.run(['gcc','-std=gnu17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(f),'-o',str(a.output/'test-object-flags')],check=True)
r=subprocess.run([str(a.output/'test-object-flags')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'));(a.output/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
