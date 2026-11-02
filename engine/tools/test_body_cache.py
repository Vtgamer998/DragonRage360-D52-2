#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Real cache/viewer regression against an earlier canonical pipeline, with mocked GPU."""
from pathlib import Path
import argparse,subprocess,os,json
p=argparse.ArgumentParser();p.add_argument('iso',type=Path);p.add_argument('reference',type=Path);p.add_argument('output',type=Path);a=p.parse_args();r=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
opts=['gcc','-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-ffp-contract=off','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(r/'src')]
ref=out/'reference.o';subprocess.run([*opts,*['-Ddr_body_pipeline_'+x+'=dr_body_pipeline_reference_'+x for x in ['build','materials','face']],'-c',str(a.reference.resolve()),'-o',str(ref)],check=True)
sources=[r/'engine/tests/body_cache_real.c',*[r/'src'/(x+'.c') for x in ['dr_body_demo','dr_body_pipeline','dr_body_model','dr_face_vif','dr_elf','dr_container','dr_disc','dr_anim','dr_bpe','dr_skeleton','dr_texfile','dr_gs_texture','dr_gs_stream','dr_vu1']]]
exe=out/'body-cache-test';subprocess.run([*opts,*map(str,sources),str(ref),'-lm','-o',str(exe)],check=True)
t=subprocess.run([str(exe),str(a.iso.resolve())],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'),timeout=120);(out/'test.log').write_text(t.stdout+t.stderr);print(t.stdout+t.stderr,end='');t.check_returncode()
(out/'verification.json').write_text(json.dumps({'result':'PASS','real_model':True,'source_pack_reads':1,'texture_uploads':15,'real_expressions':5,'matches_D33_full_interpreter_vertices_and_TEX0':True,'body_rebuild_on_expression':False,'ISO_read_on_expression':False,'cache_hits_execute_VU':False,'ASan_UBSan':True,'GPU':'mocked'},indent=2)+'\n')
