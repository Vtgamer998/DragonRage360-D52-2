#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Real USA ISO through actual platform code; NT IO replaced by host stdio."""
import argparse,subprocess,os,json,hashlib
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('xsdk',type=Path);p.add_argument('iso',type=Path);p.add_argument('output',type=Path)
p.add_argument('--sanitize',action='store_true');a=p.parse_args()
e=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
cmd=['gcc','-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-Wl,--gc-sections']
if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
cmd+=['-I'+str(d.resolve()) for d in [e/'platform/platform',e.parent/'src',a.xsdk/'include']]
cmd+=list(map(str,[e/'tests/test_xb_iso_file.c',e/'platform/platform/xb_iso_file.c',e/'platform/platform/xb_disc.c',e.parent/'src/dr_disc.c']))
exe=out/'iso-files';subprocess.run(cmd+['-o',str(exe)],check=True)
run=subprocess.run([str(exe),str(a.iso.resolve()),str(out/'SLUS_216.78'),str(out/'DBZP.BIN')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
(out/'iso-files.txt').write_text(run.stdout+run.stderr);print(run.stdout+run.stderr,end='');run.check_returncode()
expected={'SLUS_216.78':'811188ba9b416500d921cd4d9514df0cbf42f3a41a99cf5aac5a3da37171bf99','DBZP.BIN':'30f61f9c78c3859e5dd4fcfd8df6753b0f2d2c64beb924b92da5b2eaaa5e09cc'}
for name,digest in expected.items():assert hashlib.sha256((out/name).read_bytes()).hexdigest()==digest
(out/'iso-files.json').write_text(json.dumps({'result':'PASS','extracted_sha256':expected,'matches_previous_independent_extraction':True,'Sony_search_date':'unavailable, zero','sanitizers':'ASan/UBSan' if a.sanitize else 'off','NT_IO':'mocked','actual_modules':['xb_iso_file.c','xb_disc.c','dr_disc.c'],'PowerPC_execution':False,'game_executed':False},indent=2)+'\n')
