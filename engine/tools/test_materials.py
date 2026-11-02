#!/usr/bin/env python3
"""Real body -> VU1 -> TEX0/STQ -> unique TexFile material, on CPU only."""
import argparse
import os
from pathlib import Path
import subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('iso',type=Path);p.add_argument('output',type=Path);p.add_argument('--sanitize',action='store_true')
p.add_argument('--viewer-only',action='store_true',help='Retry the real-viewer test without repeating the completed CPU corpus')
p.add_argument('--viewer',type=Path,help='xsdk root; also run real-loader viewer with GPU mocks')
p.add_argument('--private-dump',type=Path,help='Optional local diagnostic assets; never include in a distribution')
a=p.parse_args();r=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(exist_ok=True,parents=True)
flags=['gcc','-std=c17','-O1','-g','-Wall','-Wextra','-Werror','-ffp-contract=off']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
for d in ['resource','vu','graphics']:flags+=['-I'+str(r/d)]
sources=[r/'tests/body_materials_real.c',r/'vu/dr_body_pipeline.c',r/'vu/dr_vu1.c',r/'graphics/dr_texfile.c',r/'graphics/dr_gs_texture.c',r/'graphics/dr_gs_stream.c']+[
    r/'resource'/(x+'.c') for x in ['dr_disc','dr_container','dr_anim','dr_bpe','dr_skeleton','dr_body_model','dr_elf','dr_face_vif']]
if not a.viewer_only:
    subprocess.run(flags+[str(s) for s in sources]+['-lm','-o',str(out/'test')],check=True)
    args=[str(out/'test'),str(a.iso.resolve())]+([str(a.private_dump.resolve())] if a.private_dump else [])
    result=subprocess.run(args,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),capture_output=True,text=True)
    (out/'result.txt').write_text(result.stdout+result.stderr)
    print(result.stdout+result.stderr,end='');result.check_returncode()

if a.viewer:
    vf=flags+['-I'+str(a.viewer.resolve()/'include')]
    more=[r/'graphics'/(x+'.c') for x in ['dr_body_demo','dr_xenos','dr_targets','dr_graphics_demo']]
    units=[r/'tests/body_demo_real.c']+sources[1:]+more
    subprocess.run(vf+[str(s) for s in units]+['-lm','-o',str(out/'viewer')],check=True)
    result=subprocess.run([str(out/'viewer'),str(a.iso.resolve())],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),capture_output=True,text=True)
    (out/'viewer-real.txt').write_text(result.stdout+result.stderr)
    print(result.stdout+result.stderr,end='');result.check_returncode()
