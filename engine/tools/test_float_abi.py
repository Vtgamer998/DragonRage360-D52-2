#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Host execution test of hard-ABI lowering against actual upstream helpers.
Not a PPC/Xbox execution or a determinism proof of the whole game.
"""
import argparse,subprocess
from pathlib import Path
import float_build
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('output',type=Path)
p.add_argument('--cc',default='gcc');p.add_argument('--host-cxx',default='g++');p.add_argument('--plugin-include',action='append',default=[])
a=p.parse_args();r=a.upstream.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
project=Path(__file__).resolve().parents[2]
plugin=float_build.build_plugin(a.cc,project,out,a.host_cxx,a.plugin_include)
common=['-O2','-ffunction-sections','-fdata-sections','-fno-strict-aliasing','-fno-builtin','-ffp-contract=off',
        '-I'+str(r/'include'),'-I'+str(r/'port/src')]
rows=[(r/'port/src/softfloat_ps2.c',float_build.helper_flags()),
      (r/'src/port/vu0_a.c',['-DREF_VU0_EXTERN_ARITH']),
      (r/'port/src/plat_fastvec.c',[]),(project/'engine/combat/math_bridge.c',[]),
      (project/'engine/tests/float_abi_probe.c',['-fplugin='+str(plugin)]),
      (project/'engine/tests/float_abi_check.c',[])]
objects=[]
for i,(source,extra) in enumerate(rows):
    dest=out/(str(i)+'.o');objects.append(dest)
    subprocess.run([a.cc,*common,*extra,'-c',str(source),'-o',str(dest)],check=True)
exe=out/'float-abi-test'
subprocess.run([a.cc,'-Wl,--gc-sections','-o',str(exe),*map(str,objects),'-lm'],check=True)
subprocess.run([str(exe)],check=True)
