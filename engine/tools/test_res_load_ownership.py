#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
from pathlib import Path
import argparse,re,subprocess,os
from fixed_profile_source import transform
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('output',type=Path);a=p.parse_args();root=Path(__file__).resolve().parents[2];up=a.upstream.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
text,_=transform('src/battle/btl_obj.c',(up/'src/battle/btl_obj.c').read_text());code=''
for name in ['BtlRes_FreeFile','BtlRes_LoadFile']:
 m=re.search(r'^[\w *]+\b'+name+r'\([^\n]*\)\s*\{',text,re.M)
 if not m:raise ValueError(name)
 end=text.index('{',m.start())+1;level=1
 while level:level+=(text[end]=='{')-(text[end]=='}');end+=1
 code+=text[m.start():end]+'\n'
src=out/'res_load_ownership.c';src.write_text((root/'engine/tests/res_load_ownership.c').read_text().replace('/* ORIGINAL_FUNCTIONS */',code));exe=out/'res-load-ownership'
r=subprocess.run(['gcc','-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(up/'include'),str(src),'-o',str(exe)],capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([str(exe)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'),timeout=10);(out/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
