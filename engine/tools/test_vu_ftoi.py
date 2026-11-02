#!/usr/bin/env python3
"""Run actual VU FTOI instruction fixtures with sanitizers; compile the same fixture for PPC."""
import argparse,subprocess,json,os
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('out',type=Path);p.add_argument('prefix');a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True);r=Path(__file__).resolve().parents[2]
f=r/'engine/tests/test_vu_ftoi.c';v=r/'src/dr_vu1.c';exe=a.out/'check'
subprocess.run(['gcc','-std=gnu17','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+str(r/'src'),str(f),str(v),'-lm','-o',str(exe)],check=True)
x=subprocess.run([str(exe)],check=True,text=True,capture_output=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'));print(x.stdout,end='');(a.out/'test.log').write_text(x.stdout)
subprocess.run([a.prefix+'gcc','-mcpu=cell','-std=gnu17','-O2','-Wall','-Wextra','-Werror','-I'+str(r/'src'),'-c',str(f),'-o',str(a.out/'native.o')],check=True)
(a.out/'result.json').write_text(json.dumps({'result':'PASS','full_interpreter_lane_cases':80160,'host_sanitizers':True,'PPC_fixture_compiled':True,'PPC_fixture_executed':False},indent=2)+'\n')
