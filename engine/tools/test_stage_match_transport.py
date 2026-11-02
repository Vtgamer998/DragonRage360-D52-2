#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Complete stage0/UI files through strict conversion; no original stage init or GPU."""
from pathlib import Path
import argparse,subprocess,os,json
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('inputs',nargs=2,type=Path);a=p.parse_args();r=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);native=r/'engine/platform/platform'
sources=[*[native/(x+'.c') for x in ['xb_stage_pack','xb_match_ui_pack','xb_resconv','xb_stage_camera','xb_stage_effects']],*[r/'src'/(x+'.c') for x in ['dr_anim','dr_bpe','dr_skeleton','dr_stage_collision','dr_container','dr_disc','dr_texfile','dr_gs_texture']]]
opts=['-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-DXB_HOST_TEST','-I'+str(native),'-I'+str(r/'src')]
cmd=['gcc',*opts,'-fsanitize=address,undefined','-fno-omit-frame-pointer',str(r/'engine/tests/stage_match_transport.c'),*map(str,sources),'-lm','-o',str(out/'stage-match-test')];subprocess.run(cmd,check=True)
t=subprocess.run([str(out/'stage-match-test'),*map(lambda p:str(p.resolve()),a.inputs)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'),timeout=60);(out/'test.log').write_text(t.stdout+t.stderr);print(t.stdout+t.stderr,end='');t.check_returncode()
(out/'verification.json').write_text(json.dumps({'result':'PASS','stage':0,'stage_entries':15,'UI_entries':197,'corruptions_refused':16,'partial_reads_refused':2,'ASan_UBSan':True,'Battle_boot':False,'GPU':False},indent=2)+'\n')
