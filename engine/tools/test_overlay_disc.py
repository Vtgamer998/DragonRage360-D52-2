#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
from pathlib import Path
import argparse,subprocess,os,json
p=argparse.ArgumentParser();p.add_argument('xsdk',type=Path);p.add_argument('iso',type=Path);p.add_argument('output',type=Path);p.add_argument('--sanitize',action='store_true');a=p.parse_args()
e=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);platform=e/'platform/platform'
cmd=['gcc','-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(platform),'-I'+str(e.parent/'src'),'-I'+str(a.xsdk.resolve()/'include')]
if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
exe=out/'overlay-disc';subprocess.run(cmd+list(map(str,[e/'tests/test_xb_overlay_disc.c',platform/'xb_overlay.c',platform/'xb_disc.c',e.parent/'src/dr_disc.c']))+['-o',str(exe)],check=True)
r=subprocess.run([str(exe),str(a.iso.resolve())],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
(out/'overlay-disc.txt').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
(out/'overlay-disc.json').write_text(json.dumps({'result':'PASS','actual_console_binding':True,'actual_ISO_reader':True,'catalogue':'host test native DATA fixture; complete engine catalogue audited separately','NT_transport':'mocked','MIPS_execution':False,'PowerPC_execution':False},indent=2)+'\n')
