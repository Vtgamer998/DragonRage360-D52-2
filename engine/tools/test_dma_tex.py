#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Compare original/adapted texture packets using PS2 arithmetic on the host."""
import argparse,hashlib,json,subprocess
from pathlib import Path
import float_build
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('output',type=Path)
p.add_argument('--cc',default='gcc');p.add_argument('--plugin-include',action='append',default=[]);p.add_argument('--sanitize',action='store_true')
a=p.parse_args();r=a.upstream.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
project=Path(__file__).resolve().parents[2];original=r/'src/sys/dma.c'
assert hashlib.sha256(original.read_bytes()).hexdigest()=='b7d254aa36fd34762d217b65cf2aa4437ef62ac40d4eba13ca80451f4b8094f3'
plugin=float_build.build_plugin(a.cc,project,out,extra_includes=a.plugin_include)
reference=out/'reference.c'
reference.write_text('#include "common.h"\n#include "sys/dma.h"\nvoid Ref_Dma_PutTexStrips('+original.read_text().split('void Dma_PutTexStrips(',1)[1])
common=['-O2','-g','-ffunction-sections','-fdata-sections','-fno-strict-aliasing','-fno-builtin','-ffp-contract=off','-fno-pie',
 '-I'+str(r/'include'),'-I'+str(r/'port/src'),'-I'+str(r/'port/include'),'-I'+str(project/'engine/graphics')]
if a.sanitize:common+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
rows=[(reference,['-include',str(r/'port/include/port_compat.h'),'-fplugin='+str(plugin)]),
 (project/'engine/combat/dma_tex.c',['-include',str(r/'port/include/port_compat.h'),'-fplugin='+str(plugin)]),
 (r/'port/src/softfloat_ps2.c',float_build.helper_flags()),
 (r/'src/port/vu0_a.c',['-DREF_VU0_EXTERN_ARITH']),(project/'engine/graphics/dr_dma_producer.c',[]),
 (project/'engine/tests/test_dma_tex.c',[])]
objects=[]
for i,(src,extra) in enumerate(rows):
 dest=out/(str(i)+'.o');objects.append(dest)
 subprocess.run([a.cc,*common,*extra,'-c',str(src),'-o',str(dest)],check=True)
exe=out/'test-texture-packets';subprocess.run([a.cc,'-no-pie',*common,'-Wl,--gc-sections',*map(str,objects),'-lm','-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True);(out/'test.txt').write_text(run.stdout+run.stderr);print(run.stdout+run.stderr,end='');run.check_returncode()
(out/'test.json').write_text(json.dumps({'result':'PASS','cases':5768,'reference':'original Dma_PutTexStrips compiled with actual PS2 arithmetic pass and helpers','source_sha256':hashlib.sha256(original.read_bytes()).hexdigest(),'sanitizers':a.sanitize,'PPC_executed':False,'GPU_executed':False,'battle_executed':False},indent=2)+'\n')
