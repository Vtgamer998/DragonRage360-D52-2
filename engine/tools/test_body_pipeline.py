#!/usr/bin/env python3
"""Build/run real-ISO body reader + original VU1 program on host. No GPU test."""
import argparse
import os
from pathlib import Path
import subprocess

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('iso', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    flags = ['gcc', '-std=c17', '-O1' if args.sanitize else '-O2', '-g',
             '-Wall', '-Wextra', '-Werror', '-ffp-contract=off']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    for include in ['resource', 'vu', 'graphics']:
        flags += ['-I' + str(root / include)]
    sources = [root / 'resource' / (s + '.c') for s in
               ['dr_disc', 'dr_container', 'dr_anim', 'dr_bpe', 'dr_skeleton',
                'dr_body_model', 'dr_elf', 'dr_face_vif']]
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    for name, extra, run_args in [
        ('body-reader', [], []),
        ('body-pipeline-real', [root/'vu/dr_vu1.c', root/'vu/dr_body_pipeline.c', root/'graphics/dr_gs_stream.c'], [str(args.iso.resolve())])
    ]:
        test = root / 'tests' / ('test_body_model.c' if name == 'body-reader' else 'body_pipeline_real.c')
        binary = out / name
        subprocess.run(flags + [str(test)] + [str(p) for p in sources + extra] + ['-lm', '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)] + run_args, env=env, text=True, capture_output=True)
        (out / (name + '.txt')).write_text(result.stdout + result.stderr)
        print(result.stdout, end='')
        print(result.stderr, end='')
        result.check_returncode()
    print('Host-only validation; no PowerPC execution, Xenos pixels, animation or combat tested.')

if __name__ == '__main__':
    main()
