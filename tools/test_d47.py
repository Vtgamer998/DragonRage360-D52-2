#!/usr/bin/env python3
"""Source-only CLUT regression and renderer lifetime checks; no native toolchain required."""
from pathlib import Path
import json, os, subprocess
r=Path(__file__).resolve().parents[1];out=r/'validation-d47/final';out.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1')
jobs=[['python3','engine/tools/test_clut_identity.py','build/d47-clut'],['python3','engine/tools/test_texture_pressure.py','build/d47-pressure'],['python3','engine/tools/test_gs_targets.py','validation-tooling/xsdk','build/d47-gs-targets','--sanitize'],['make','host-test'],['python3','adaptation/tools/audit_original.py','--output','build/d47-original']]
for i,cmd in enumerate(jobs):
 with (out/f'check-{i+1}.log').open('w') as log:subprocess.run(cmd,cwd=r,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 print(f'PASS {i+1}/{len(jobs)} {cmd[1]}',flush=True)
(out/'summary.json').write_text(json.dumps({'result':'PASS','groups':len(jobs),'scope':'host regressions and original source audit','XEX_built':False,'GPU_execution':False,'hardware_FPS_measured':False},indent=2)+'\n')
