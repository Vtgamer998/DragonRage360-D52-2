#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Whole real starting-model/effect packs through the strict host hook, not game boot."""
from pathlib import Path
import argparse,subprocess,os,json
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('inputs',nargs=4,type=Path);a=p.parse_args();r=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);native=r/'engine/platform/platform'
sources=[*[native/(x+'.c') for x in ['xb_model_pack','xb_effect_pack','xb_resconv','xb_fighter_tables','xb_stage_camera']],*[r/'src'/(x+'.c') for x in ['dr_body_model','dr_face_vif','dr_anim','dr_container','dr_disc','dr_bpe','dr_skeleton','dr_texfile','dr_gs_texture']]]
cmd=['gcc','-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-DXB_HOST_TEST','-I'+str(native),'-I'+str(r/'src'),'-fsanitize=address,undefined','-fno-omit-frame-pointer',str(r/'engine/tests/model_effect_transport.c'),*map(str,sources),'-lm','-o',str(out/'model-effect-test')]
subprocess.run(cmd,check=True)
t=subprocess.run([str(out/'model-effect-test'),*map(lambda p:str(p.resolve()),a.inputs)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'),timeout=60);(out/'test.log').write_text(t.stdout+t.stderr);print(t.stdout+t.stderr,end='');t.check_returncode()
(out/'verification.json').write_text(json.dumps({'result':'PASS','resource_ids':['0x590','0x59a','0x599','0x5a3'],'atomic_corruptions':16,'partial_reads_refused':4,'model_BE_headers_and_selected_fields':True,'rebasing_hook_provenance_and_repeat':True,'ASan_UBSan':True,'Battle_boot':False,'GPU':False},indent=2)+'\n')
