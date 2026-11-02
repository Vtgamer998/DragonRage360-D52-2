#!/usr/bin/env python3
"""Check native DATE/FBA and stencil-restore microcode on xost's CPU simulator.
Usage: test_gs_buffers.py <xsdk> <glslang> <out>
This is not GPU, eDRAM, tiled-pass or rasterization verification.
"""
import itertools
import json
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


def program(name):
    source = engine / 'graphics/shaders' / name
    spv = out / (name + '.spv')
    subprocess.run([str(glslang), '-V', str(source), '-o', str(spv)],
                   check=True, stdout=subprocess.DEVNULL)
    blob, manifest, resources = native.compile_spirv(
        spv.read_bytes(), XSDK_REGISTERS.pixel, XSDK_REGISTERS.vertex, optimize=True)
    (out / (name + '.native')).write_bytes(blob)
    constants = {reg: list(value) for reg, value in manifest if isinstance(reg, int)}
    return blob, constants, resources


blob, constants, resources = program('dr_gs.frag')
assert ('push', 0, 0, 0, 4) in resources
constants[0] = [1.0, 1.0, 1.0, 0.0]  # textured DECAL, TCC, ATST off
constants[2] = [1.0 / 1280, 1.0 / 1024, 0.0, 0.0]
constants[255] = [0.5, 0.5, 0.0, 0.0]
material_cases = 0
for alpha, fba, date, stored in itertools.product(
        (0, 1, 63, 64, 127, 128, 129, 254, 255), (0, 1), (0, 1, 2), (0, 127, 128, 255)):
    constants[1] = [0.0, 0.0, float(date), float(fba)]
    texture = [0.25, 0.5, 0.75, alpha / 255]
    sampled = set()

    def fetch(slot, uv):
        sampled.add(slot)
        assert slot in (0, 1)
        return texture if slot == 0 else [stored / 255, 0.0, 0.0, 1.0]

    result = simulate.run(blob, {0: [1.0] * 4, 1: [0.25, 0.5, 1.0, 0.0],
                                 2: [0.5, 1.0, 0.0, 0.0], 3: [16.0, 32.0, 0.0, 0.0]}, constants, texture_fetch=fetch)
    passes = date == 0 or ((stored & 128) != 0) == (date == 2)
    assert result.killed != passes, (alpha, fba, date, stored, result.killed)
    if passes:
        colour, aux = result.exports[0], result.exports[1]
        expected = alpha | (128 if fba else 0)
        assert abs(aux[0] - expected / 255) < 3e-6, (alpha, fba, aux, expected)
        assert abs(colour[3] - min(alpha / 128, 1.0)) < 3e-6
    assert sampled == {0, 1}
    material_cases += 1

blob, constants, resources = program('dr_gs_target.frag')
constants[0] = [2.0, 0.0, 1.0, 0.0]
constants[1] = [0.0, 0.0, 0.0, 0.0]
constants[2] = [1.0 / 1280, 1.0 / 1024, 0.0, 0.0]
constants[3] = [0.0, 0.0, 1.0, 1.0]
constants[255] = [0.5, 0.5, 0.0, 0.0]
target_cases = 0
# Color alpha is deliberately wrong: only the independent raw-alpha texture
# can retain bit 7 / values >128 through buffer copies and GS alpha tests.
for tfx, raw_alpha, tcc, vertex_alpha, atst in itertools.product(
        range(4), (0, 1, 63, 64, 127, 128, 129, 254, 255), (0, 1), (0.5, 1.0), range(9)):
    constants[0] = [2.0, float(tfx), float(tcc), float(atst)]
    constants[1] = [128.0, 0.0, 0.0, 0.0]
    sampled = set()
    def fetch_target(slot, uv):
        sampled.add(slot)
        assert slot in (0, 1, 2)
        return [0.25, 0.5, 0.75, 0.0] if slot == 0 else [raw_alpha / 255, 0.0, 0.0, 1.0]
    result = simulate.run(blob, {0: [1.0, 1.0, 1.0, vertex_alpha], 1: [0.25, 0.5, 1.0, 0.0],
                                 2: [0.5, 1.0, 0.0, 0.0], 3: [16.0, 32.0, 0.0, 0.0]}, constants,
                          texture_fetch=fetch_target)
    ta = raw_alpha / 128
    expected = vertex_alpha if not tcc else ta * vertex_alpha if tfx == 0 else ta + vertex_alpha if tfx == 2 else ta
    ag = int(expected * 128 + 0.5)
    passes = (True, False, True, ag < 128, ag <= 128, ag == 128, ag >= 128, ag > 128, ag != 128)[atst]
    assert result.killed != passes, (tfx, raw_alpha, tcc, vertex_alpha, atst, result)
    if passes:
        assert abs(result.exports[0][3] - min(expected, 1.0)) < 3e-6
        assert abs(result.exports[1][0] - min(ag, 255) / 255) < 3e-6
    assert sampled == {0, 1, 2}
    target_cases += 1
constants[0] = [2.0, 0.0, 1.0, 0.0]
constants[1] = [0.0, 0.0, 0.0, 0.0]

clamp_cases=0
constants[3]=[0.125,0.25,0.75,0.875]
for s,t,q in itertools.product([-2,0.25,2],[-1,0.5,2],[0.5,1,2]):
    def fetch_clamped(slot,uv):
        if slot in (0,2):
            want=[min(max(s/q,0.125),0.75),min(max(t/q,0.25),0.875)]
            assert max(abs(a-b) for a,b in zip(uv[:2],want))<2e-6,(uv,want)
        return [0.25,0.5,0.75,1]
    result=simulate.run(blob,{0:[1]*4,1:[s,t,q,0],2:[0.5,1,0,0],3:[16,32,0,0]},constants,texture_fetch=fetch_clamped)
    assert not result.killed
    clamp_cases+=1

blob, constants, resources = program('dr_gs_stencil.frag')
assert ('push', 0, 0, 0, 1) in resources
stencil_cases = 0
for stencil, bit in itertools.product(range(256), (0, 1, 2, 4, 8, 16, 32, 64, 128)):
    constants[0] = [float(bit), 0.0, 0.0, 0.0]
    # Resolve bytes Dhi,Dmid,Dlo,S; only the last byte must affect stencil.
    sample = [0.25, 0.75, 0.5, stencil / 255]
    result = simulate.run(blob, {0: [0.5, 0.5, 0.0, 0.0]}, constants,
                          texture_fetch=lambda slot, uv: sample)
    assert result.killed == (bit != 0 and (stencil & bit) == 0), (stencil, bit)
    stencil_cases += 1

report = {'result': 'PASS', 'DATE_FBA_native_microcode_cases': material_cases,
          'sampled_target_native_microcode_cases': target_cases,
          'stencil_native_microcode_cases': stencil_cases,'target_UV_clamp_cases':clamp_cases,
          'scope': 'sampled target raw-alpha (including >128), TFX/TCC/ATST, raw-alpha MRT output, DATE bit7 decision, FBA OR 128, eight stencil-bit selectors and clear selector',
          'GPU_execution': False, 'rasterization': False,
          'not_verified': ['real D24S8/RGBA8 alias bytes', 'eDRAM clears/restores', 'tiled execution', 'hardware pixels']}
(out / 'gs-buffers-shaders.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2), flush=True)
