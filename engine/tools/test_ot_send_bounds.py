#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
import subprocess,argparse,os
from pathlib import Path
from fixed_profile_source import transform
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('output',type=Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);e=Path(__file__).resolve().parents[1]
s,_=transform('src/sys/gfx_ot.c',(a.upstream/'src/sys/gfx_ot.c').read_text());s=s[s.index('static int XbOt_Range'):s.index('/* Queues the GS state')]
f=a.output/'ot-send.c';f.write_text((e/'tests/ot_send_bounds.c').read_text().replace('/* ORIGINAL_FUNCTIONS */',s))
subprocess.run(['gcc','-std=gnu17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(f),'-o',str(a.output/'test-ot')],check=True)
r=subprocess.run([str(a.output/'test-ot')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'));(a.output/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
