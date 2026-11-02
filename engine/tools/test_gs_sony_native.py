#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Test native libgraph environment serialization with the real session."""
from pathlib import Path
import argparse,subprocess
p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path);p.add_argument('--sanitize',action='store_true')
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);e=Path(__file__).resolve().parents[1]
cmd=['gcc','-std=gnu17','-DDR_XBOX360','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(e/'capture/port/src/gs'),'-O1','-g','-Wall','-Wextra','-Werror','-ffp-contract=off','-I'+str(e/'graphics'),'-I'+str(e/'vu')]
if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
# Exercise the exact native SDK entry point without a GPU/backend in this transport test.
shim=out/'shim.c';shim.write_text('#include "dr_gs_native.h"\nvoid Port_GsGifChannel(uint32_t a,uint32_t n,int c){dr_gs_native_gif(a,n,c);}\n')
files=[e/'tests/test_gs_sony_native.c',e/'capture/port/src/gs/gs_sony.c',e/'graphics/dr_gs_native.c',e/'graphics/dr_gs_stream.c',e/'vu/dr_vu1.c',shim]
cmd+=list(map(str,files))+['-lm','-o',str(out/'sony-native')];subprocess.run(cmd,check=True)
r=subprocess.run([str(out/'sony-native')],capture_output=True,text=True);(out/'sony-native.txt').write_text(r.stdout+r.stderr)
print(r.stdout+r.stderr,end='');r.check_returncode()
