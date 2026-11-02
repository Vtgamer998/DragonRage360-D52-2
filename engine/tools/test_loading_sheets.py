#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Original screen setup with real sheets on host. IO/fixup mocked; no GPU."""
import argparse,subprocess,os
from pathlib import Path
from loading_error_source import transform
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('output',type=Path);p.add_argument('sheets',nargs=3,type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
original=(a.upstream/'src/sys/loading.c').read_text();adapted=transform('src/sys/loading.c',original)
macros=original[original.index('#define SPRITE(p,'):original.index('/* Shows the loading screen')]
functions=adapted[adapted.index('void Load_BuildSprites(void) {'):adapted.index('/* Advances the animation')]
fixture=(root/'engine/tests/loading_sheets.c').read_text();src=out/'loading_sheets.c';src.write_text(fixture.replace('/* ORIGINAL_FUNCTIONS */',macros+functions))
exe=out/'loading-sheets';platform=root/'engine/platform/platform'
sources=[src,*[platform/n for n in ['xb_resconv.c','xb_bpe.c','xb_demo_resources.c']],*[root/'src'/n for n in ['dr_bpe.c','dr_texfile.c','dr_gs_texture.c','dr_container.c']]]
cmd=['gcc','-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-DXB_HOST_TEST','-I'+str(root/'src'),'-I'+str(platform),'-I'+str(a.upstream.resolve()/'include'),*map(str,sources),'-Wl,--gc-sections','-o',str(exe)]
r=subprocess.run(cmd,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([str(exe),*map(lambda x:str(x.resolve()),a.sheets)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'),timeout=20);(out/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
