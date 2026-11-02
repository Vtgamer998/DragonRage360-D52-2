#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Validate the bounded D38 original-frame run; never infer a complete fight."""
import argparse,json,re,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('log',type=Path);p.add_argument('--run',type=Path);p.add_argument('--xex',type=Path);p.add_argument('--image',type=Path);p.add_argument('--recovery-log',type=Path);p.add_argument('--output',type=Path);a=p.parse_args();s=a.log.read_text(errors='replace')
assert 'DragonRage360 D38: native diagnostic;' in s and 'data_stripped=2;' in s
frames=re.findall(r'D38 original Battle_StepFrame frame=(\d+) returned=(-?\d+) read_error=(-?\d+)',s)
assert frames==[(str(i),'0','0') for i in range(64)],frames
assert 'iterations=64 submitted;' in s
assert 'type=0 begin=156 end=156 phase4=1 result=0' in s
assert 'res: 28 files converted, 0 reads with no converter' in s
assert 'res: 0 conversions rejected' in s
assert 'D38 fresh close result=0 complete=1;' in s
assert 'typed-data diagnostic returned to UI loop; scene=0 stage=0' in s
assert all(l=='FAIL: interface first frame orange tile' for l in s.splitlines() if l.startswith('FAIL:'))
assert not any(x in s for x in ['GS Xenos FAIL','VU budget exhausted','VIF submit error=','VIF CALL rejected','DMA first error='])
visibility=re.findall(r'D38 visible objects frame=(\d+) views=(\d+) flags=([0-9a-f]+) list0=(\d+) list1=(\d+)',s)
assert [x[0] for x in visibility]==['0','63']
r={'result':'PASS','bounded_Update_Draw_steps':64,'original_frame_pairs':156,'converted':28,'rejected':0,'Battle_Main_called':False,'NOT_READY':2,'physical_GPU_tested':False,'audible_audio_proven':False,'complete_fight_proven':False,'visibility_samples':visibility,'fighters_presented':False,'stage_external_image_checked':False}
if a.run:
 run=json.loads(a.run.read_text());r['run']=run
 if a.xex:assert run['xex_sha256']==hashlib.sha256(a.xex.read_bytes()).hexdigest()
if a.image:
 from PIL import Image
 im=Image.open(a.image).convert('RGB');assert im.size==(1280,768)
 sky=im.getpixel((900,100));grass=im.getpixel((900,600));assert sky[2]>sky[0]+10 and grass[1]>grass[0]*1.2 and grass[1]>grass[2]*1.2
 assert len(im.crop((0,25,1280,745)).getcolors(921601))>2000
 r.update(stage_external_image_checked=True,sky_RGB=sky,grass_RGB=grass,image_sha256=hashlib.sha256(a.image.read_bytes()).hexdigest())
if a.recovery_log:
 t=a.recovery_log.read_text(errors='replace')
 assert 'injected completed-frame error=-956' in t
 assert 'D38 fresh close result=-955 complete=1;' in t
 assert 'PASS: D38 rejected frame recovery gate/backend=0;' in t
 assert t.count('D38 actual Battle_Init returned=0 read_error=0')==2
 assert 'D38 fresh close result=0 complete=1;' in t
 assert 'res: 50 files converted, 0 reads with no converter' in t
 assert 'typed-data diagnostic returned to UI loop' in t
 r['post_Init_draw_failure_recovery']='PASS: injected presentation error after GPU wait; fresh Init in same executable; general partial-init/script recovery not proven'
if a.output:a.output.write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps(r,indent=2))
