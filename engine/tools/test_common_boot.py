#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Common ownership with simulated devices; exact adapted original functions."""
from pathlib import Path
import argparse,subprocess,re,os
from fixed_profile_source import transform
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[2];up=a.upstream.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
def function(text,name):
 m=re.search(r'^[\w *]+\b'+name+r'\([^\n]*\)\s*\{',text,re.M)
 if not m:raise ValueError(name)
 end=text.index('{',m.start())+1;level=1
 while level:level+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[m.start():end]+'\n'
text,_=transform('src/sys/common.c',(up/'src/sys/common.c').read_text())
code='\n'.join(function(text,n) for n in ['Common_Init','Common_LoadBoot','Common_Reload'])
src=out/'common_boot.c';fixture=(root/'engine/tests/common_boot_ownership.c').read_text().replace('/* ORIGINAL_FUNCTIONS */','#define File_RequestSized(id,p,n) File_Request(id,p)\n'+code);src.write_text(fixture)
exe=out/'common-boot'
cmd=['gcc','-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-DXB_HOST_TEST','-I'+str(up/'include'),'-I'+str(root/'engine/platform/platform'),str(src),str(root/'engine/platform/platform/xb_resconv.c'),'-o',str(exe)]
r=subprocess.run(cmd,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([str(exe)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'),timeout=15);(out/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
# The production engine substitutes its own main.c. Both must stop on a common failure.
for path in [up/'src/main.c',root/'engine/combat/main.c']:
 adapted,_=transform('src/main.c',path.read_text())
 for call in ['Common_Init();','Common_LoadBoot();','Common_Reload();']:
  assert call+'\n    { extern int XbRes_LastError(void); if (XbRes_LastError()) return; }' in adapted
print('PASS common failure guards in upstream and engine Game_Main source; not executable startup proof')
