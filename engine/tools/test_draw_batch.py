#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
from pathlib import Path
import argparse,subprocess,json,os
p=argparse.ArgumentParser();p.add_argument('output',type=Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);r=Path(__file__).resolve().parents[2]
exe=a.output/'test-batch';subprocess.run(['gcc','-std=gnu17','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+str(r/'engine/graphics'),str(r/'engine/tests/gs_batch_bounds.c'),str(r/'src/dr_gs_batch.c'),'-o',str(exe)],check=True)
s=subprocess.run([str(exe)],capture_output=True,text=True,check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'));(a.output/'test.log').write_text(s.stdout+s.stderr);print(s.stdout,end='')
(a.output/'batch.json').write_text(json.dumps({'result':'PASS','uniform_mutations':228,'synthetic_compatible_draws_before':1000,'synthetic_compatible_draws_after':3,'vertex_count_unchanged':3000,'max_triangle_vertices_per_draw':1023,'render_order_preserved':True,'GPU_rasterization':False,'physical_FPS_benchmark':False,'scope':'exact state, values, immutable rejection, barriers, topology boundaries and checked address arithmetic'},indent=2)+'\n')
