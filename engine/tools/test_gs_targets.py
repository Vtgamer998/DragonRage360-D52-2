#!/usr/bin/env python3
"""GS surfaces and snapshots with API/lifetime mocks, not a GPU rasterizer.
Usage: test_gs_targets.py <xsdk> <out> [--sanitize]
"""
import json
import os
from pathlib import Path
import subprocess
import sys

engine = Path(__file__).resolve().parents[1]
sdk, out = (Path(p).resolve() for p in sys.argv[1:3])
out.mkdir(parents=True, exist_ok=True)
cmd = ['gcc', '-std=gnu17', '-O2', '-Wall', '-Wextra', '-Werror',
       '-I' + str(engine / 'graphics'), '-I' + str(sdk / 'include'),
       str(engine / 'tests/test_gs_targets.c')]
cmd += [str(engine / 'graphics' / f) for f in
        ['dr_xenos.c', 'dr_targets.c', 'dr_gs_texture.c', 'dr_graphics_demo.c']]
cmd += ['-lm', '-o', str(out / 'test-gs-targets')]
if '--sanitize' in sys.argv:
    cmd[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g']
subprocess.run(cmd, check=True)
result = subprocess.run([str(out / 'test-gs-targets')], check=True, capture_output=True, text=True)
(out / 'gs-targets.txt').write_text(result.stdout)
report = {'result': 'PASS', 'scope': ['GS MRT/tiled call order', 'A/B/A color/raw-alpha/depth/stencil model',
          'independent DATE and effect snapshots', 'target sampling and feedback resolve with independent raw-alpha binding and matching sampler',
          'timeout ownership and bounded closed-renderer timeout selection', 'recording failure cleanup', 'live SDK cursor with stale count', 'pre-draw command split preserves color/raw-alpha/depth/stencil', 'exact margin and invalid cursor rejection', 'snapshot resolve pitch matches active eDRAM source pitch at 1024 and 64 pixels', 'DATE/outline/CLUT coordinates use actual borrowed-view dimensions; allocation owner stays unchanged'], 'GPU_execution': False,
          'not_verified': ['actual D24S8/RGBA8 alias', 'tiling bands on hardware', 'rasterized pixels'],
          'sanitizers': 'ASan/UBSan' if '--sanitize' in sys.argv else 'not enabled',
          'leak_detection': 'disabled' if 'detect_leaks=0' in os.environ.get('ASAN_OPTIONS', '') else 'runtime default'}
(out / 'gs-targets.json').write_text(json.dumps(report, indent=2) + '\n')
print(result.stdout, end='')
print(json.dumps(report, indent=2))
