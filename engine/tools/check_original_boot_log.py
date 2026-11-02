#!/usr/bin/env python3
"""Verify the three original D38 loading cycles and optional actual Xenia PNGs."""
import argparse,json,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('log',type=Path);p.add_argument('--images',type=Path);p.add_argument('--output',type=Path);a=p.parse_args();s=a.log.read_text(errors='replace')
assert 'DragonRage360 D38: native diagnostic;' in s
assert 'data_stripped=2;' in s
assert s.count('mode=5')==3
assert s.count('PASS: D38 original File_Init: partitions0,1 and six ADXT players initialized')==3
results=re.findall(r'PASS: D38 full original load type=(\d) begin=(\d+) end=(\d+) phase4=1 result=0',s)
assert [int(x[0]) for x in results]==[0,1,2],results
assert all(int(b)>0 and b==e for t,b,e in results)
assert s.count('PASS: D38 actual Battle_Load result=1 objects=2 error=0;')==3
assert s.count('D38 fresh close result=0 complete=1;')==3
assert 'res: 60 files converted, 0 reads with no converter' in s
assert 'res: 0 conversions rejected' in s
f=re.search(r'D38 original loading presentation frames=(\d+) result=0',s);assert f
assert int(f[1])==sum(int(b) for t,b,e in results)
assert 'typed-data diagnostic returned to UI loop; scene=0 stage=0' in s
assert 'GS Xenos FAIL' not in s
fails=[l for l in s.splitlines() if l.startswith('FAIL:')]
assert all(l=='FAIL: interface first frame orange tile' for l in fails),fails
r={'result':'PASS','original_File_Init_ADX':True,'six_ADXT_players_per_cycle':True,'original_Load_RunBlocking':True,'original_Gfx_BeginFrame_EndFrame':True,'types':[0,1,2],'frames':int(f[1]),'frames_per_type':[int(b) for t,b,e in results],'converted':60,'rejected':0,'NOT_READY':2,'battle_frame_proven':False,'physical_GPU_proven':False,'audible_audio_proven':False,'internal_pixel_fixed':False,'external_images':[]}
if a.images:
 from PIL import Image
 for t,color in enumerate([(34,73,104),(70,16,18),(31,82,24)]):
  fn=a.images/('loading-'+str(t)+'.png');im=Image.open(fn).convert('RGB');assert im.size==(1280,768)
  pixels=list(im.crop((0,25,1280,745)).get_flattened_data());bg=sum(p==color for p in pixels);fg=len(pixels)-bg
  assert bg>10000 and fg>1000,(t,bg,fg)
  r['external_images'].append({'type':t,'background_RGB':color,'background_pixels':bg,'different_pixels':fg,'note':'actual emulator window; not a battle frame'})
if a.output:a.output.write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps(r,indent=2))
