#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Real motion files through original consumers on host; no fight or GPU."""
from pathlib import Path
import argparse,re,subprocess,os
from fixed_profile_source import transform
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('output',type=Path);p.add_argument('motions',nargs=2,type=Path);a=p.parse_args();up=a.upstream.resolve();root=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
text,_=transform('src/battle/bobj_a.c',(up/'src/battle/bobj_a.c').read_text())
code=''
for name in ['BObjFile_GetEntry','BtlObjAnim_Load']:
 m=re.search(r'^[\w *]+\b'+name+r'\([^\n]*\)\s*\{',text,re.M)
 if not m:raise ValueError(name)
 end=text.index('{',m.start())+1;level=1
 while level:level+=(text[end]=='{')-(text[end]=='}');end+=1
 code+=text[m.start():end]+'\n'
src=out/'motion_transport.c';src.write_text((root/'engine/tests/test_motion_transport.c').read_text().replace('/* ORIGINAL_FUNCTIONS */',code));platform=root/'engine/platform/platform';exe=out/'motion-transport'
sources=[src,*[platform/n for n in ['xb_motion_pack.c','xb_resconv.c','xb_bpe.c']],*[root/'src'/n for n in ['dr_container.c','dr_anim.c','dr_bpe.c','dr_disc.c']]]
cmd=['gcc','-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-DXB_HOST_TEST','-I'+str(platform),'-I'+str(root/'src'),'-I'+str(up/'include'),*map(str,sources),'-Wl,--gc-sections','-o',str(exe)]
r=subprocess.run(cmd,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([str(exe),*map(lambda p:str(p.resolve()),a.motions)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'),timeout=60);(out/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
