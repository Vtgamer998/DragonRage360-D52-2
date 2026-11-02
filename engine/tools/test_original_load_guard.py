#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
from pathlib import Path
import argparse,re,subprocess
from loading_error_source import transform
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('output',type=Path);a=p.parse_args();up=a.upstream.resolve();root=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);platform=root/'engine/platform/platform'
text=transform('src/sys/file.c',(up/'src/sys/file.c').read_text());code=''
for name in ['File_InitRequests','File_GetPendingCount','File_IsBusy']:
 m=re.search(r'^[\w *]+\b'+name+r'\([^\n]*\)\s*\{',text,re.M);assert m,name
 end=text.index('{',m.start())+1;level=1
 while level:level+=(text[end]=='{')-(text[end]=='}');end+=1
 code+=text[m.start():end]+'\n'
src=out/'original_load_guard.c';src.write_text((root/'engine/tests/test_original_load_guard.c').read_text().replace('/* ORIGINAL_FUNCTIONS */',code))
flags=['-std=gnu17','-O1','-g','-fno-builtin','-ffunction-sections','-fdata-sections','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(up/'include'),'-I'+str(up/'port/include'),'-I'+str(platform)]
objs=[]
for source in [platform/'xb_fighter_registry.c',*[up/'src/sys'/f'{n}.c' for n in ['list','queue','job']]]:
 obj=out/(source.stem+'.o');r=subprocess.run(['gcc',*flags,'-c',str(source),'-o',str(obj)],capture_output=True,text=True);(out/(source.stem+'-compile.log')).write_text(r.stdout+r.stderr);r.check_returncode();objs.append(obj)
exe=out/'original-load-guard';r=subprocess.run(['gcc',*flags,'-Wall','-Wextra','-Werror','-DXB_HOST_TEST',str(src),str(platform/'xb_resconv.c'),*map(str,objs),'-Wl,--gc-sections','-o',str(exe)],capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10);(out/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
