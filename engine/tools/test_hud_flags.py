#!/usr/bin/env python3
"""Exercise transformed original HUD setters against the renderer's numeric masks."""
from pathlib import Path
import argparse,re,subprocess,os
from fixed_profile_source import transform
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('output',type=Path);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
def body(s,name):
 m=re.search(r'void '+name+r'\([^\n]*\)\s*\{',s);assert m;end=s.index('{',m.start())+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
functions=''
for file,names in [('src/battle/hud_a.c',['Hud_ShowAll','Hud_ShowGauges','Hud_ShowTimer','Hud_ShowNotice','Hud_ShowCombo','Hud_ShowPrompt']),('src/battle/hud_c_c.c',['HudSprite_Show','HudSprite_SetMirror'])]:
 s,_=transform(file,(a.upstream/file).read_text());functions+='\n'.join(body(s,n) for n in names)+'\n'
f=out/'check.c';f.write_text('''#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef int s32;typedef uint32_t u32;typedef struct {u32 flags;} Hud,HudCSprite;static Hud work,*gHud=&work;
'''+functions+'''
int main(void){for(int on=-3;on<=3;on++){work.flags=0xa0000000;Hud_ShowAll(on);assert(work.flags==(0xa0000000u|((on&1)?31:0)));}
void(*set[5])(s32)={Hud_ShowGauges,Hud_ShowTimer,Hud_ShowNotice,Hud_ShowCombo,Hud_ShowPrompt};for(unsigned i=0;i<5;i++){work.flags=0x80000000;set[i](1);assert(work.flags==(0x80000000u|(1u<<i)));set[i](0);assert(work.flags==0x80000000u);}
HudCSprite p={0x80000000};HudSprite_Show(&p,0);assert(p.flags==0x80000001);HudSprite_SetMirror(&p,1);assert(p.flags==0x80000003);HudSprite_Show(&p,1);assert(p.flags==0x80000002);HudSprite_SetMirror(&p,2);assert(p.flags==0x80000000);puts("PASS actual HUD setters: five low show bits, hidden/mirror masks, unrelated bits retained, original one-bit truncation");}
''');exe=out/'check';subprocess.run(['gcc','-std=gnu17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie',str(f),'-o',str(exe)],check=True);r=subprocess.run([str(exe)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'));(out/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
