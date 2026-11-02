#!/usr/bin/env python3
"""Native multi-target call/lifetime tests on host; no GPU execution.
Usage: test_targets.py <xsdk> <out> [--sanitize]
"""
import json,os,pathlib,subprocess,sys
engine=pathlib.Path(__file__).resolve().parents[1];sdk=pathlib.Path(sys.argv[1]).resolve()
out=pathlib.Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
cmd=['gcc','-std=gnu17','-O2','-Wall','-Wextra','-Werror','-I'+str(engine/'graphics'),'-I'+str(sdk/'include'),
     str(engine/'tests/test_targets.c'),*[str(engine/'graphics'/n) for n in
     ['dr_targets.c','dr_xenos.c','dr_graphics_demo.c','dr_gs_texture.c']],'-lm','-o',str(out/'test-targets')]
if '--sanitize' in sys.argv:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g']
subprocess.run(cmd,check=True);subprocess.run([str(out/'test-targets')],check=True)
report={'result':'PASS','coverage':['A/B/A color/depth call model','native draw core integration',
        '24 target IDs','128MiB page-rounded peak budget','partial allocation failure preserves old target',
        'GPU timeout never frees in-flight memory','normal diagnostic batches do not lose stored targets'],
        'limits':['no GPU rasterization','no stencil restore','no GS aux/DATE','no engine registration',
                  'no game assets','no PowerPC execution','no new XEX'],
        'sanitizers':'ASan/UBSan' if '--sanitize' in sys.argv else 'not enabled',
        'leak_detection':'disabled' if 'detect_leaks=0' in os.environ.get('ASAN_OPTIONS','') else 'runtime default'}
(out/'targets.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
