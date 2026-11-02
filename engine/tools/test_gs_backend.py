#!/usr/bin/env python3
"""Real backend callbacks and patched GsGpu_Init selection, using GPU/platform mocks.
Usage: test_gs_backend.py <upstream> <xsdk> <out> [--sanitize]
The GsGpu_Init body is extracted verbatim; the rest of gs_draw.c is not linked.
"""
import json
import os
from pathlib import Path
import subprocess
import sys

engine = Path(__file__).resolve().parents[1]
repo, sdk, out = (Path(p).resolve() for p in sys.argv[1:4])
out.mkdir(parents=True, exist_ok=True)
source = (repo / 'port/src/gs/gs_draw.c').read_text()
start = source.index('int GsGpu_Init(void) {')
body = source[start:source.index('\n}\n', start) + 3]
assert 'sb = &sXenosBackend;' in body and '#ifdef DR_XBOX360' in body
generated = out / 'registration.c'
generated.write_text('''#define DR_XBOX360 1
#define DR_BACKEND_TEST_MAIN previous_backend_tests
#include "test_gs_backend.c"
#undef main
static GsBackend *sBackend;
int gsPendingScale4,gsGlowPercent,gsFilter,gGsAstcOn,gGsFragDepth;
unsigned gsFxOff;
GsTex gsWhite;
static int sTexPackOn,sSmooth2d,sHudLayout;
int gPortMusicPercent,gPortSePercent,gPortVoicePercent,gPortStagePercent;
static int Port_Setting(const char *name,int fallback) {(void)name;return fallback;}
''' + body + '''
int main(void) {
    previous_backend_tests();
    assert(setenv("BT3_GPU_API","xenos",1)==0);
    assert(GsGpu_Init()==1 && sBackend==&sXenosBackend && gsWhite==sXenosBackend.whiteTex());
    assert(gsVerts && gsVuVerts && gsVuIdx && gsVuUni && gsDraws && gsScale4==4);
    assert(dr_gs_backend_close()==0);
    free(gsVerts);free(gsVuVerts);free(gsVuIdx);free(gsVuUni);free(gsDraws);
    puts("PASS: patched GsGpu_Init body selects sXenosBackend under DR_XBOX360, no GL/Vulkan linked; remaining capture source not compiled");
}
''')
cmd = ['gcc', '-std=gnu17', '-O2', '-Wall', '-Wextra', '-Werror',
       '-I' + str(engine / 'tests'), '-I' + str(engine / 'graphics'),
       '-I' + str(repo / 'port/src/gs'), '-I' + str(sdk / 'include'), str(generated)]
cmd += [str(engine / 'graphics' / f) for f in
        ['dr_gs_backend.c', 'dr_gs_replay.c','dr_gs_batch.c', 'dr_xenos.c', 'dr_targets.c',
         'dr_gs_texture.c', 'dr_graphics_demo.c']]
cmd += ['-lm', '-o', str(out / 'test-gs-backend')]
if '--sanitize' in sys.argv:
    cmd[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g']
subprocess.run(cmd, check=True)
result = subprocess.run([str(out / 'test-gs-backend')], check=True, capture_output=True, text=True)
(out / 'gs-backend.txt').write_text(result.stdout)
report = {'result': 'PASS', 'backend': 'real sXenosBackend object and callbacks',
          'registration': 'verbatim patched GsGpu_Init body, DR_XBOX360; rest of gs_draw.c not compiled',
          'scope': ['texture/mip upload ownership and handle generations', 'DATE marker/FBA replay',
                    'MRT target handles', 'present/read/color-copy calls, raw-alpha source binding and destination MRT write mask', 'unknown VU fails before draw'],
          'GPU_execution': False, 'complete_engine_linked': False,
          'sanitizers': 'ASan/UBSan' if '--sanitize' in sys.argv else 'not enabled',
          'leak_detection': 'disabled' if 'detect_leaks=0' in os.environ.get('ASAN_OPTIONS', '') else 'runtime default'}
(out / 'gs-backend.json').write_text(json.dumps(report, indent=2) + '\n')
print(result.stdout, end='')
print(json.dumps(report, indent=2))
