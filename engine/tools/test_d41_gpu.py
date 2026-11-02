#!/usr/bin/env python3
"""Bounded frame arena and immutable command-ring lifetime tests; no GPU rasterization."""
from pathlib import Path
import argparse,subprocess,os,json
p=argparse.ArgumentParser();p.add_argument('xsdk',type=Path);p.add_argument('output',type=Path);a=p.parse_args();e=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
for name in ['frame_arena','command_ring']:
 exe=out/name;cmd=['gcc','-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+str(e/'graphics'),'-I'+str(a.xsdk.resolve()/'include'),str(e/'tests'/f'test_{name}.c')]
 cmd += [str(e/'graphics'/n) for n in ['dr_xenos.c','dr_targets.c','dr_graphics_demo.c','dr_gs_texture.c']]+['-lm','-o',str(exe)]
 subprocess.run(cmd,check=True);r=subprocess.run([str(exe)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'));(out/f'{name}.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
(out/'result.json').write_text(json.dumps({'result':'PASS','frame_vertices':262144,'vertex_bytes':12582912,'command_slots':8,'command_bytes_per_slot':4194300,'tests':'frame overflow before GPU; immutable fetches; eight unique recordings; partial allocation rollback; timeout ownership; safe retry and close','GPU_rasterization':False},indent=2)+'\n')
