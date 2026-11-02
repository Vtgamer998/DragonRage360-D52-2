#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
import argparse,re,subprocess,os
from pathlib import Path
from fixed_profile_source import transform
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('output',type=Path);a=p.parse_args();root=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
s,_=transform('src/battle/eft_ae.c',(a.upstream/'src/battle/eft_ae.c').read_text())
def function(name):
 m=re.search(r'^void '+name+r'\([^\n]*\)\s*\{',s,re.M);assert m,name
 end=s.index('{',m.start())+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
code='\n'.join(function(n) for n in ['BtlTask_Init','BtlTask_SetDead','BtlTaskList_KillAll','BtlTask_Kill','BtlTaskList_Update','BtlTaskList_Reset','BtlTaskList_Draw'])
assert 'BTL_TASK_BITS(' not in code
src=out/'task-flags.c';src.write_text((root/'engine/tests/task_flags.c').read_text().replace('/* ORIGINAL_FUNCTIONS */',code))
r=subprocess.run(['gcc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-overflow','-fsanitize=address,undefined','-I'+str(a.upstream.resolve()/'include'),str(src),'-o',str(out/'task-flags')],capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([str(out/'task-flags')],capture_output=True,text=True,timeout=10,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'));(out/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
