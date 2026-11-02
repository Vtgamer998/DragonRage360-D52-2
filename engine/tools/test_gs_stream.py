#!/usr/bin/env python3
"""Bounded stream and original reference VM; no battle/GPU execution."""
from pathlib import Path
import argparse,json,subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('output',type=Path);p.add_argument('--sanitize',action='store_true')
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
engine=Path(__file__).resolve().parents[1]
cmd=['gcc','-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-ffp-contract=off',
     '-I'+str(engine/'graphics'),'-I'+str(engine/'vu')]
if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
cmd+=list(map(str,[engine/'tests/test_gs_stream.c',engine/'graphics/dr_gs_stream.c',engine/'vu/dr_vu1.c']))
cmd+=['-lm','-o',str(out/'test-stream')];subprocess.run(cmd,check=True)
r=subprocess.run([str(out/'test-stream')],capture_output=True,text=True)
(out/'stream.txt').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
(out/'stream.json').write_text(json.dumps({'result':'PASS','sanitizers':'ASan/UBSan' if a.sanitize else 'off',
    'scope':'eight DMA IDs, exact nonoverlapping address spans, TTE and fragmented payloads, VIF controls, original reference VU, bounds/budgets/error propagation',
    'battle_executed':False,'GPU_execution':False,'original_native_packet_producers_adapted':False},indent=2)+'\n')
