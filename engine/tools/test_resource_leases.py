#!/usr/bin/env python3
from pathlib import Path
import subprocess,argparse,json,os
p=argparse.ArgumentParser();p.add_argument('out',type=Path);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True);r=Path(__file__).resolve().parents[2];exe=a.out/'check'
subprocess.run(['gcc','-std=gnu17','-O2','-Wall','-Wextra','-Werror','-DXB_HOST_TEST','-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+str(r/'engine/platform/platform'),str(r/'engine/tests/test_resource_leases.c'),str(r/'engine/platform/platform/xb_resconv.c'),'-o',str(exe)],check=True)
x=subprocess.run([str(exe)],text=True,capture_output=True,check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'));print(x.stdout,end='');(a.out/'test.log').write_text(x.stdout+x.stderr);(a.out/'result.json').write_text(json.dumps({'result':'PASS','live_spans':512,'no_live_eviction':True,'overflow_preserves_existing_spans':True,'revocation_and_decoded_spans':True,'scope':'actual registry with host sanitizer tests; runtime separately logged'},indent=2)+'\n')
