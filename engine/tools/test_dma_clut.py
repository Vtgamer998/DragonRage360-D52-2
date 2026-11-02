#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
import argparse,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('output',type=Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);e=Path(__file__).resolve().parents[1]
subprocess.run(['gcc','-std=gnu17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(e/'graphics'),str(e/'tests/test_dma_clut.c'),str(e/'graphics/dr_dma_producer.c'),'-o',str(a.output/'test-clut')],check=True)
r=subprocess.run([str(a.output/'test-clut')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'));(a.output/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
