#!/usr/bin/env python3
"""Viewer batching and resource lifetime with mocked GPU calls; no rasterization."""
import argparse
import json
import os
from pathlib import Path
import subprocess

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('xsdk', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--sanitize', action='store_true')
    a = p.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    flags = ['gcc', '-std=gnu17', '-O1', '-g', '-Wall', '-Wextra', '-Werror']
    if a.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    for include in [a.xsdk.resolve()/'include', root/'resource', root/'vu', root/'graphics']:
        flags += ['-I' + str(include)]
    sources = [root/'tests/test_body_demo.c'] + [root/'graphics'/(n + '.c') for n in
               ['dr_body_demo', 'dr_xenos', 'dr_targets', 'dr_graphics_demo', 'dr_gs_texture']]
    binary = out/'test-body-demo'
    subprocess.run(flags + [str(x) for x in sources] + ['-lm', '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'),
                            text=True, capture_output=True)
    (out/'body-demo.txt').write_text(result.stdout + result.stderr)
    print(result.stdout + result.stderr, end='')
    result.check_returncode()
    (out/'body-demo.json').write_text(json.dumps({
        'result': 'PASS', 'gpu_execution': False, 'model_loader': 'synthetic stub',
        'native_draw_core': 'real implementation', 'max_chunk_vertices': 1023,
        'coverage': ['fence before arena reuse', 'timeout retains GPU resources',
                     'capacity batching', 'close and reopen', 'load failure cleanup'],
        'sanitizers': 'ASan/UBSan' if a.sanitize else 'disabled'
    }, indent=2) + '\n')

if __name__ == '__main__':
    main()
