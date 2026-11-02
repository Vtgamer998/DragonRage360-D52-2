#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Initial-job ownership fault injection, with fake devices/resources."""
from pathlib import Path
import argparse,re,subprocess,os
from fixed_profile_source import transform
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('output',type=Path);a=p.parse_args();root=Path(__file__).resolve().parents[2];up=a.upstream.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
text,_=transform('src/battle/battle_load.c',(up/'src/battle/battle_load.c').read_text());code=''
for name in ['BtlLoad_StepInitial','XbBtlLoad_AbortInitial']:
 m=re.search(r'^[\w *]+\b'+name+r'\([^\n]*\)\s*\{',text,re.M)
 if not m:raise ValueError(name)
 end=text.index('{',m.start())+1;level=1
 while level:level+=(text[end]=='{')-(text[end]=='}');end+=1
 code+=text[m.start():end]+'\n'
src=out/'initial_load_ownership.c';src.write_text((root/'engine/tests/initial_load_ownership.c').read_text().replace('/* ORIGINAL_FUNCTIONS */',code));exe=out/'initial-load-ownership';platform=root/'engine/platform/platform'
heap_obj=out/'heap.o';align_obj=out/'align.o'
flags=['-std=gnu17','-O1','-g','-fno-builtin','-ffunction-sections','-fdata-sections','-fsanitize=undefined','-I'+str(up/'include'),'-I'+str(platform)]
for source,obj,extra in [(root/'engine/combat/heap.c',heap_obj,['-DPORT_BIGENDIAN','-Dmalloc=TestHeapMalloc','-DHeap_Alloc=ActualHeap_Alloc','-DHeap_Free=ActualHeap_Free']),(up/'src/sys/align.c',align_obj,[])]:
 r=subprocess.run(['gcc',*flags,*extra,'-c',str(source),'-o',str(obj)],capture_output=True,text=True);(out/(obj.stem+'-compile.log')).write_text(r.stdout+r.stderr);r.check_returncode()
cmd=['gcc','-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-DXB_HOST_TEST','-I'+str(up/'include'),'-I'+str(platform),str(src),str(platform/'xb_resconv.c'),str(heap_obj),str(align_obj),'-Wl,--gc-sections','-o',str(exe)]
r=subprocess.run(cmd,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([str(exe)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'),timeout=30);(out/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
