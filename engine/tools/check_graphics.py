#!/usr/bin/env python3
"""Compile native graphics modules with xsdk and optionally regenerate shaders.
Usage: check_graphics.py <xsdk> <powerpc-xsdk-elf-gcc> <out> [glslang]
No console execution. Regeneration needs xsdk/build/host/libxsc.so.
"""
import json,pathlib,subprocess,sys
engine=pathlib.Path(__file__).resolve().parents[1]
sdk=pathlib.Path(sys.argv[1]).resolve();cc=str(pathlib.Path(sys.argv[2]).resolve())
out=pathlib.Path(sys.argv[3]).resolve();out.mkdir(parents=True,exist_ok=True)
graphics=engine/'graphics'
if len(sys.argv)>4:
    subprocess.run([sys.executable,str(sdk/'tools/build_shaders.py'),'--glslang',str(pathlib.Path(sys.argv[4]).resolve()),'--output',str(graphics/'dr_gs_shaders.h'),str(graphics/'shaders/dr_gs.vert'),str(graphics/'shaders/dr_gs.frag'),str(graphics/'shaders/dr_gs_target.frag')],check=True)
    subprocess.run([sys.executable,str(sdk/'tools/build_shaders.py'),'--glslang',str(pathlib.Path(sys.argv[4]).resolve()),'--output',str(graphics/'dr_restore_shaders.h'),str(graphics/'shaders/dr_restore.vert'),str(graphics/'shaders/dr_restore.frag')],check=True)
    subprocess.run([sys.executable,str(sdk/'tools/build_shaders.py'),'--glslang',str(pathlib.Path(sys.argv[4]).resolve()),'--output',str(graphics/'dr_gs_targets_shaders.h'),str(graphics/'shaders/dr_gs_restore.frag'),str(graphics/'shaders/dr_gs_stencil.frag')],check=True)
compiled=[]
for p in sorted(graphics.glob('*.c')):
    if p.name in ('dr_gs_replay.c','dr_gs_backend.c'):
        continue # Engine ABI consumer: use test_gs_replay.py with actual upstream headers.
    subprocess.run([cc,'-std=gnu17','-O2','-Wall','-Wextra','-Werror','-I'+str(sdk/'include'),'-I'+str(graphics),'-c',str(p),'-o',str(out/p.with_suffix('.o').name)],check=True)
    compiled.append(p.name)
report={'target':'powerpc-xsdk-elf','compiled':compiled,'shaders':'GLSL -> SPIR-V -> open xost/xsc native containers','scope':'native drawing core, saved color/depth targets and GS VRAM texture decoder; not full GsBackend or linked game engine','hardware':'not executed'}
(out/'graphics-powerpc.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
