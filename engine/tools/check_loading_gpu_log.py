#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Check a D36 fresh-gpu run and an optional external emulator screenshot."""
import argparse,json,re
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);p.add_argument('--image',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
s=a.log.read_text(errors='replace')
assert 'DragonRage360 D36: native diagnostic;' in s
assert 'data_stripped=2;' in s
assert s.count('mode=4')==2
assert s.count('PASS: D36 actual Battle_Load result=1 objects=2 error=0;')==2
assert s.count('D36 fresh close result=0 complete=1;')==2
assert s.count('phase4=1 complete=1 polls=')==2
assert 'res: 40 files converted, 0 reads with no converter' in s
assert 'res: 0 conversions rejected' in s
assert 'D36 original renderer opened; no synthetic packets submitted' in s
assert 'PASS: D36 original Load_DrawScreen -> DMA/VIF/GIF -> gs_draw -> Xenos GPU submit result=0;' in s
frames=re.search(r'D36 original loading presentation frames=(\d+) result=0',s);assert frames and int(frames[1])>=2
assert 'typed-data diagnostic returned to UI loop; scene=0 stage=0' in s
assert 'GS Xenos FAIL' not in s
fails=[l for l in s.splitlines() if l.startswith('FAIL:')]
assert all(l=='FAIL: interface first frame orange tile' for l in fails),fails
r={'result':'PASS','original_Load_DrawScreen_submitted':True,'presented_loading_frames':int(frames[1]),'Battle_Load_successes':2,'objects_per_load':2,'converted_files':40,'NOT_READY':2,'internal_interface_readback_failed':bool(fails),'Battle_Init_Update_Main':False,'battle_frame_proven':False,'audible_audio_proven':False,'physical_GPU_proven':False,'external_screenshot_checked':False}
if a.image:
 from PIL import Image
 im=Image.open(a.image).convert('RGB');assert im.size==(1280,768)
 color=re.search(r'D36 draw 0 .*rgba=(\d+),(\d+),(\d+),128',s);assert color
 expected=tuple(map(int,color.groups()));pixels=list(im.crop((0,25,1280,745)).get_flattened_data())
 background=sum(p==expected for p in pixels);foreground=len(pixels)-background
 assert background>10000 and foreground>1000,(background,foreground)
 r.update(external_screenshot_checked=True,background_RGB=expected,background_pixels=background,foreground_pixels=foreground,screenshot_note='actual Xenia window; loading screen, not battle')
if a.output:a.output.write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps(r,indent=2))
