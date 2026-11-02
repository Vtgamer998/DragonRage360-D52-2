#!/usr/bin/env python3
"""Run the compiled native material shader's microcode on xost's CPU simulator.

Usage: test_gs_materials.py <xsdk> <glslang> <out>
No GPU, rasterization, texture filtering or Xbox execution is involved.
"""
import itertools
import json
import math
from pathlib import Path
import subprocess
import sys

engine = Path(__file__).resolve().parents[1]
sdk, glslang, out = (Path(p).resolve() for p in sys.argv[1:4])
out.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(sdk / 'xenos-toolchain'))
from xsc import native  # noqa: E402
from xdis import simulate  # noqa: E402
from xasm.program import XSDK_REGISTERS  # noqa: E402

spv = out / 'material.spv'
subprocess.run([str(glslang), '-V', str(engine / 'graphics/shaders/dr_gs.frag'),
                '-o', str(spv)], check=True, stdout=subprocess.DEVNULL)
blob, manifest, resources = native.compile_spirv(
    spv.read_bytes(), XSDK_REGISTERS.pixel, XSDK_REGISTERS.vertex, optimize=True)
(out / 'material.native').write_bytes(blob)
constants = {reg: list(value) for reg, value in manifest if isinstance(reg, int)}
assert ('push', 0, 0, 0, 4) in resources
assert ('sampler', 0, 0, 0, 1) in resources


def reference(textured, tfx, tcc, atst, ref, colour, texel, premultiply):
    # Independently evaluate the original gs.frag equations, using byte inputs.
    rgb = [c / 255 for c in colour[:3]]
    alpha = colour[3] / 128
    if textured:
        ta = texel[3] / 128
        trgb = [c / 255 for c in texel[:3]]
        if tfx == 0:
            rgb = [t * c / 128 for t, c in zip(trgb, colour)]
            if tcc:
                alpha *= ta
        elif tfx == 1:
            rgb = trgb
            if tcc:
                alpha = ta
        else:
            rgb = [t * c / 128 + colour[3] / 255 for t, c in zip(trgb, colour)]
            if tcc:
                alpha = ta + alpha if tfx == 2 else ta
    ag = math.floor(alpha * 128 + 0.5)
    passes = [True, False, True, ag < ref, ag <= ref, ag == ref,
              ag >= ref, ag > ref, ag != ref]
    if not passes[atst]:
        return None
    if premultiply:
        rgb = [c * alpha for c in rgb]
    return [min(1.0, max(0.0, c)) for c in rgb + [alpha]]


count = killed = 0
for textured, tfx, tcc, atst, alpha, ref, premultiply in itertools.product(
        (0, 1), range(4), (0, 1), range(9), (0, 64, 127, 128, 255),
        (0, 127, 128, 255), (0, 1)):
    colour = [32, 128, 224, 128]
    texel = [192, 64, 128, alpha]
    constants[0] = [float(textured), float(tfx), float(tcc), float(atst)]
    constants[1] = [float(ref), float(premultiply), 0.0, 0.0]
    constants[2] = [1.0 / 1280, 1.0 / 1024, 0.0, 0.0]
    result = simulate.run(blob, {0: [c / 128 for c in colour], 1: [0.25, 0.5, 1.0, 0.0], 2: [0.5,1.0,0.0,0.0]},
                          constants, texture_fetch=lambda slot, uv: [c / 255 for c in texel])
    want = reference(textured, tfx, tcc, atst, ref, colour, texel, premultiply)
    got = None if result.killed else result.exports.get(0)
    assert (got is None) == (want is None), (constants[0], alpha, ref, got, want)
    if want is not None:
        assert got is not None and max(abs(a - b) for a, b in zip(got, want)) < 3e-6, (got, want)
    count += 1
    killed += got is None
report = {'result': 'PASS', 'compiled_microcode_cases': count, 'discarded_cases': killed,
          'scope': 'TFX 0..3, TCC, ATST off/0..7, AREF boundaries, source-alpha prescaling',
          'GPU_execution': False, 'rasterization': False, 'texture_filtering': False,
          'DATE_FBA_stencil': 'covered separately by test_gs_buffers.py',
          'reference': 'original gs.frag equations; CPU microcode simulator is not hardware evidence'}
(out / 'materials-shader.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2), flush=True)
