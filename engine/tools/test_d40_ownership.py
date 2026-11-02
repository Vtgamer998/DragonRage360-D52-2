#!/usr/bin/env python3
"""ASan/UBSan regression checks for decoded-resource and owned-model lifetimes."""
from pathlib import Path
import subprocess,argparse,os
p=argparse.ArgumentParser();p.add_argument('output',type=Path);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);e=Path(__file__).resolve().parents[1];plat=e/'platform/platform'
s=(plat/'xb_model_native.c').read_text();actual=s[s.index('int XbNativeModel_ChainSpan('):s.index('void XbNativeTexture_Free')];f=out/'model-span.c';f.write_text((e/'tests/model_span_bounds.c').read_text().replace('/* ACTUAL_SPAN_FUNCTION */',actual))
for name,src in [('model-span',[f]),('decoded-span',[plat/'xb_resconv.c',e/'tests/test_decoded_span.c']),('checked-reads',[plat/'xb_resconv.c',e/'tests/test_checked_reads.c']),('embedded-sources',[plat/'xb_resconv.c',e/'tests/test_embedded_sources.c'])]:
 exe=out/name
 subprocess.run(['gcc','-std=gnu17','-Wall','-Wextra','-Werror','-DXB_HOST_TEST','-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+str(plat),*map(str,src),'-o',str(exe)],check=True)
 r=subprocess.run([str(exe)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'));(out/f'{name}.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
