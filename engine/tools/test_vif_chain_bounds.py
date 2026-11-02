#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
import subprocess,argparse,os
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('output',type=Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);e=Path(__file__).resolve().parents[1]
s=(e/'platform/platform/xb_fresh_boot.c').read_text();s=s[s.index('u32 XbVif_ChainAddress(u32 ptr) {'):]
f=a.output/'vif-chain.c';f.write_text((e/'tests/vif_chain_bounds.c').read_text().replace('/* ORIGINAL_FUNCTION */',s))
subprocess.run(['gcc','-std=gnu17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(f),'-o',str(a.output/'test-chain')],check=True)
r=subprocess.run([str(a.output/'test-chain')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'));(a.output/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
