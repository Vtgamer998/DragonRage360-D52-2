#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Inventory a fixed 0-vs-1/stage-0 profile using actual bounded native readers.
Conversion failures are explicitly reported; a readable file is not a typed,
relocated, combat-ready resource. No source resource bytes are saved.
"""
from pathlib import Path
import argparse,subprocess,json,os
p=argparse.ArgumentParser();p.add_argument('xsdk',type=Path);p.add_argument('iso',type=Path);p.add_argument('output',type=Path);p.add_argument('--sanitize',action='store_true');p.add_argument('--coverage',type=Path);a=p.parse_args();e=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
cmd=['gcc','-D_GNU_SOURCE','-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-I'+str(e/'platform/platform'),'-I'+str(e.parent/'src'),'-idirafter',str(a.xsdk.resolve()/'include')]
if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
fixture=e/'tests/test_fixed_resources.c'
if a.coverage:
 coverage=json.loads(a.coverage.read_text());ranges=coverage['verified_ranges'];assert coverage['logical_size']==a.iso.stat().st_size
 adapter=(e/'tests/test_xb_iso_file.c').read_text()
 guard='static int verified_read(uint64_t a,size_t n){static const uint64_t ranges[][2]={'+','.join('{'+str(lo)+','+str(hi)+'}' for lo,hi in ranges)+'};for(size_t i=0;i<sizeof(ranges)/sizeof(*ranges);i++)if(ranges[i][0]<=a && a<=ranges[i][1] && n<=ranges[i][1]-a)return 1;return 0;}\n'
 needle='    if(fseeko(image,(off_t)*at,SEEK_SET))'
 assert needle in adapter
 adapter=adapter.replace(needle,'    assert(verified_read(*at,n));\n'+needle)
 adapter=adapter.replace('static const char *image_path;',guard+'static const char *image_path;')
 (out/'test_xb_iso_file.c').write_text(adapter)
 fixture=out/'test_fixed_resources.c';fixture.write_text((e/'tests/test_fixed_resources.c').read_text())

sources=[fixture,e/'platform/platform/xb_disc.c',e/'platform/platform/xb_iso_file.c',*[e.parent/'src'/(s+'.c') for s in ['dr_disc','dr_container','dr_stage_collision','dr_body_model','dr_face_vif','dr_anim','dr_bpe','dr_skeleton','dr_texfile','dr_gs_texture']]]
exe=out/'fixed-resources';subprocess.run(cmd+list(map(str,sources))+['-lm','-o',str(exe)],check=True)
r=subprocess.run([str(exe),str(a.iso.resolve())],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'resources.txt').write_text(r.stdout);(out/'conversion-errors.txt').write_text(r.stderr);r.check_returncode();rows=[json.loads(s) for s in r.stdout.splitlines()];assert len(rows)==18
report={'profile':{'fighters':[0,1],'costumes':[0,0],'stage':0,'split_screen':False,'voice_alt':False},'file_id_evidence':'include/battle/battle_work.h + src/battle/battle_load.c, pinned upstream; AFS index = file_id - 1',
 'result':'readers validated; partial conversions, NOT combat-ready','resources':rows,'sanitizers':a.sanitize,'PPC_execution':False,'live_battle_resources_initialized':False,'stage_collision_reader_validated':True,'stage_collision_and_physics_installed_in_engine':False,'sparse_input_coverage_checked':bool(a.coverage)}
(out/'fixed-resource-manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'resources':len(rows),'body_parts':[r['typed_body_parts'] for r in rows if r['typed_body_parts']],'animations_converted':sum(r['animations_converted'] for r in rows),'empty_animation_slots':sum(r['empty_animation_slots'] for r in rows),'animations_rejected':sum(r['animation_rejected'] for r in rows),'game_ready':False},indent=2))
