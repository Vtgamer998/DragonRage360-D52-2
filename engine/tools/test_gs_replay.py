#!/usr/bin/env python3
"""Test actual pinned gs_draw.h descriptors against native renderer call mocks.
Usage: test_gs_replay.py <upstream> <xsdk> <out> [--sanitize]
"""
import json,os,pathlib,subprocess,sys
engine=pathlib.Path(__file__).resolve().parents[1]
repo=pathlib.Path(sys.argv[1]).resolve();sdk=pathlib.Path(sys.argv[2]).resolve()
out=pathlib.Path(sys.argv[3]).resolve();out.mkdir(parents=True,exist_ok=True)
cmd=['gcc','-std=gnu17','-O2','-Wall','-Wextra','-Werror',
     '-I'+str(engine/'graphics'),'-I'+str(repo/'port/src/gs'),'-I'+str(sdk/'include'),
     str(engine/'tests/test_gs_replay.c'),str(engine/'graphics/dr_gs_replay.c'),str(engine/'graphics/dr_gs_batch.c'),
     str(engine/'graphics/dr_xenos.c'),str(engine/'graphics/dr_targets.c'),str(engine/'graphics/dr_gs_texture.c'),
     str(engine/'graphics/dr_graphics_demo.c'),'-lm','-o',str(out/'test-gs-replay')]
if '--sanitize' in sys.argv:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-fno-pie','-no-pie']
subprocess.run(cmd,check=True)
subprocess.run([str(out/'test-gs-replay')],check=True)
report={'result':'PASS','ABI':'original pinned port/src/gs/gs_draw.h',
        'scope':'single diagnostic surface or multiple saved color/depth targets, actual GsDraw descriptors, ordinary CPU vertices, TFX/TCC/ATST, blend-key translation, color masks, GS DATE/FBA and snapshots via separate backend test, complete-list validation',
        'unsupported':'remaining native effects, original GS mip registers, complete GS precision/masks; outline/CLUT and VU tested separately through full capture',
        'immutable_replay_validation_reused':True,'late_invalid_vertex_prevents_all_emission':True,'GPU_execution':False,'XEX_rebuilt':False,'game_engine_linked':False,
        'sanitizers':'ASan/UBSan' if '--sanitize' in sys.argv else 'not enabled',
        'leak_detection':'disabled' if 'detect_leaks=0' in os.environ.get('ASAN_OPTIONS','') else 'runtime default'}
(out/'gs-replay.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
