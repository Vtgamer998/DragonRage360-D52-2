#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run the actual native data loader with synthetic sources and writable maps."""
from pathlib import Path
import argparse,subprocess,json,os
p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path);p.add_argument('--sanitize',action='store_true')
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);e=Path(__file__).resolve().parents[1];platform=e/'platform/platform'
cmd=['gcc','-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-DXB_HOST_TEST','-I'+str(platform)]
if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
cmd+=list(map(str,[e/'platform/tools/test_xb_gamedata.c',platform/'xb_gamedata.c']))+['-o',str(out/'test-data')]
subprocess.run(cmd,check=True);r=subprocess.run([str(out/'test-data')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
(out/'data-test.txt').write_text(r.stdout+r.stderr);print(r.stdout,end='')
if r.returncode:print(r.stderr[-3000:])
r.check_returncode();(out/'data-test.json').write_text(json.dumps({'result':'PASS','sanitizers':a.sanitize,'synthetic_sources_only':True,'PowerPC_execution':False,'real_ISO_data_loaded':False,'battle_executed':False},indent=2)+'\n')
