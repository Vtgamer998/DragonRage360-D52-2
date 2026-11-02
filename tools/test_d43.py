#!/usr/bin/env python3
"""Repeat source-only D43 validation. No PowerPC toolchain, game assets or GPU required."""
from pathlib import Path
import argparse,json,os,subprocess
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=Path('build/validation-d43'));a=p.parse_args()
r=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);sdk=r/'validation-tooling/xsdk';env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1')
commands=[['python3',str(r/'engine/tools/test_gs_targets.py'),str(sdk),str(out/'gs-targets'),'--sanitize'],['python3',str(r/'engine/tools/test_targets.py'),str(sdk),str(out/'targets'),'--sanitize'],['python3',str(r/'engine/tools/test_model_gpu.py'),'--vuasm',str(r/'validation-tooling/vuasm.py'),'--output',str(out/'model-gpu')],['python3',str(r/'engine/tools/test_vu_compiled.py'),str(sdk),str(out/'compiled-shaders')],['python3',str(r/'adaptation/tools/audit_original.py'),'--output',str(out/'original-source')],['python3',str(r/'adaptation/tests/test_publish.py')],['gcc','-std=gnu17','-Wall','-Wextra','-Werror','-DDR_XBOX360','-fsyntax-only','-I'+str(sdk/'include'),'-I'+str(r/'src'),'-I'+str(r/'engine/graphics'),'-I'+str(r/'engine/capture/port/src/gs'),'-I'+str(r/'engine/platform/platform'),*[str(r/'src'/f) for f in ['dr_vu1.c','dr_capture_demo.c','dr_targets.c','dr_xenos.c']]]]
for i,cmd in enumerate(commands):
 with (out/f'test-{i+1}.log').open('w') as log:subprocess.run(cmd,cwd=r,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 print(f'PASS {i+1}/{len(commands)} {cmd[1]}',flush=True)
(out/'summary.json').write_text(json.dumps({'result':'PASS','tests':len(commands),'scope':'host/source only; no GPU, PowerPC, XEX, whole game or FPS proof'},indent=2)+'\n')
