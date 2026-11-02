#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Exercise the six actual adapted ki helpers with observable output-parameter dependencies."""
from pathlib import Path
import argparse,re,subprocess,json,os
from fixed_profile_source import transform
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('prefix');p.add_argument('output',type=Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
text,_=transform('src/battle/btl_tech_b.c',(a.upstream/'src/battle/btl_tech_b.c').read_text())
names=['BtlKiBlast_GetKiCost','BtlKiBlast_GetKiCostOfHit','BtlKiBlast_GetSpeed','BtlKiBlast_GetSpeedOfHit','BtlKiBlast_GetReact','BtlKiBlast_GetReactOfHit']
functions=[]
for name in names:
 m=re.search(r'^(s32|f32) '+name+r'\([^\n]+\) \{',text,re.M);assert m
 end=text.index('\n}',m.start())+2;functions.append(text[m.start():end])
h=(a.upstream/'include/battle/btl_tech_b.h').read_text();data=re.search(r'typedef struct BtlKiBlastData \{.*?\} BtlKiBlastData;',h,re.S)[0]
source='''#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <math.h>
typedef uint8_t u8;typedef int8_t s8;typedef uint16_t u16;typedef int32_t s32;typedef float f32;
typedef struct {int id;} BtlTechBChr;typedef struct {int id;} BtlTechBHit;
#define BTL_KMH_PER_FRAME (1.0f/108.0f)
'''+data+'''
static BtlTechBChr sender={123};static BtlKiBlastData record={.ki=1750,.speed=3.25f,.react=23};
static int phase,selected;
static BtlKiBlastData *BtlKiBlast_GetCurrent(BtlTechBChr *chr,s32 *kind){assert(chr==&sender && phase==0);*kind=selected;phase=1;return &record;}
static BtlKiBlastData *BtlKiBlast_GetOfHit(BtlTechBHit *hit,BtlTechBChr **chr,s32 *kind){assert(hit && phase==0);*chr=&sender;*kind=selected;phase=1;return &record;}
static void consume(BtlTechBChr *chr,int kind){assert(phase==1 && chr==&sender && kind==selected);phase=2;}
static s32 BtlKiBlast_ScaleKiByAbility(BtlTechBChr *chr,s32 ki,s32 kind){consume(chr,kind);assert(ki==1750);return ki+kind;}
static f32 BtlKiBlast_ScaleSpeedByAbility(BtlTechBChr *chr,s32 kind,f32 speed){consume(chr,kind);assert(fabsf(speed-record.speed*10.0f*BTL_KMH_PER_FRAME)<1e-6f);return speed+kind;}
static s32 BtlKiBlast_MapReact(BtlTechBChr *chr,s32 react,s32 kind){consume(chr,kind);assert(react==23);return react+kind;}
'''+ '\n'.join(functions)+'''
int main(void){BtlTechBHit hit={1};for(selected=0;selected<13;selected++){
phase=0;assert(BtlKiBlast_GetKiCost(&sender)==1750+selected && phase==2);
phase=0;assert(BtlKiBlast_GetKiCostOfHit(&hit)==1750+selected && phase==2);
phase=0;(void)BtlKiBlast_GetSpeed(&sender);assert(phase==2);
phase=0;(void)BtlKiBlast_GetSpeedOfHit(&hit);assert(phase==2);
phase=0;assert(BtlKiBlast_GetReact(&sender)==23+selected && phase==2);
phase=0;assert(BtlKiBlast_GetReactOfHit(&hit)==23+selected && phase==2);
}puts("PASS 78 ki helper cases: owner and kind produced before consumers, original cost/speed/reaction fields forwarded");}
'''
f=a.output/'fixture.c';f.write_text(source);exe=a.output/'test-ki'
subprocess.run(['gcc','-std=gnu17','-O2','-Wall','-Wextra','-Werror','-Werror=uninitialized','-fsanitize=address,undefined','-fno-pie','-no-pie',str(f),'-lm','-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True,check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'));(a.output/'test.log').write_text(r.stdout+r.stderr);print(r.stdout,end='')
subprocess.run([a.prefix+'gcc','-mcpu=cell','-O2','-std=gnu17','-Wall','-Wextra','-Werror=uninitialized','-c',str(f),'-o',str(a.output/'native.o')],check=True)
(a.output/'ki-sequencing.json').write_text(json.dumps({'result':'PASS','cases':78,'source':'six actual adapted helpers from pinned btl_tech_b.c','host_sanitizers':'ASAN/UBSAN','PPC_compiled':True,'PPC_fixture_executed':False,'scope':'dependency ordering and owner/kind/value forwarding; native gameplay evidence separate'},indent=2)+'\n')
