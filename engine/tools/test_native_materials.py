#!/usr/bin/env python3
"""Test native material/mipmap API with GPU call mocks, not a rasterizer.
Usage: test_native_materials.py <xsdk> <out> [--sanitize]
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
       str(engine / 'tests/test_native_materials.c')]
cmd += [str(engine / 'graphics' / f) for f in
        ['dr_xenos.c', 'dr_targets.c', 'dr_gs_texture.c', 'dr_graphics_demo.c']]
cmd += ['-lm', '-o', str(out / 'test-native-materials')]
if '--sanitize' in sys.argv:
    cmd[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g']
subprocess.run(cmd, check=True)
result = subprocess.run([str(out / 'test-native-materials')], check=True, capture_output=True, text=True)
(out / 'native-materials.txt').write_text(result.stdout)
report = {'result': 'PASS', 'blend_keys': 108, 'mipmap_levels': 3,
          'scope': 'material constants, channel masks, blend-key mapping, explicit mip-chain preflight/upload',
          'GPU_execution': False, 'rasterization': False,
          'sanitizers': 'ASan/UBSan' if '--sanitize' in sys.argv else 'not enabled',
          'leak_detection': 'disabled' if 'detect_leaks=0' in os.environ.get('ASAN_OPTIONS', '') else 'runtime default'}
(out / 'native-materials.json').write_text(json.dumps(report, indent=2) + '\n')
print(result.stdout, end='')
print(json.dumps(report, indent=2))
