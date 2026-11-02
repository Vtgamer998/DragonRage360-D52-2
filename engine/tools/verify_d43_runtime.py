#!/usr/bin/env python3
"""Verify a D43 original-battle-check log and the exact tested XEX; no Xbox/FPS claim."""
from pathlib import Path
import argparse,hashlib,json,re

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('run',type=Path);p.add_argument('expected_xex',type=Path);p.add_argument('output',type=Path)
a=p.parse_args();log=(a.run/'storage/cache/default.log').read_text(errors='replace')
assert 'DragonRage360 D43:' in log and 'data_stripped=2' in log
assert 'D43 actual Battle_Init returned=0 read_error=0' in log
health=re.findall(r'PASS: D43 starting health side=(\d+) value=(\d+) maximum=(\d+)',log)
assert {int(s) for s,_,_ in health}=={0,1} and all(int(v)>0 and v==m for _,v,m in health)
assert not any(x in log for x in ['GS GPU failure','GS Xenos FAIL','FAIL: D43','frame rejected'])
match=re.search(r'D43 experimental battle completed_steps=(\d+) FIGHT_frames=(\d+) cancelled=(\d+) natural_end=(\d+) limit=(\d+)',log)
assert match,'No completed battle/check record'
steps,fights,cancel,natural,limit=map(int,match.groups());assert steps>0 and fights==4 and cancel==1 and natural==0
frames=re.search(r'PASS: D43 full original load type=0 begin=(\d+) end=(\d+) phase4=1 result=0',log)
assert frames and int(frames[1])>0 and frames[1]==frames[2]
assert 'D43 fresh close result=0 complete=1; NOT_READY unchanged' in log
assert 'typed-data diagnostic returned to UI loop' in log and 'res: 0 conversions rejected' in log
gpu=re.findall(r'D43 model GPU draws=(\d+) fallbacks=(\d+) command_splits=(\d+)',log)
assert gpu and int(gpu[-1][0])>0 and int(gpu[-1][1])==0 and int(gpu[-1][2])>0
tested=hashlib.sha256((a.run/'default.xex').read_bytes()).hexdigest()
assert tested==hashlib.sha256(a.expected_xex.read_bytes()).hexdigest()
report={'result':'PASS','tested_XEX_sha256':tested,'renderer':'Xenia Vulkan llvmpipe software; not Xbox performance',
        'Battle_Init':'PASS','health':health,'completed_steps':steps,'FIGHT_frames':fights,
        'cancelled_between_frames':bool(cancel),'natural_KO_verified':False,'begin_frames':int(frames[1]),'end_frames':int(frames[2]),
        'transaction_close_result':0,'last_logged_model_GPU_draws':int(gpu[-1][0]),'last_logged_model_fallbacks':0,
        'last_logged_command_splits':int(gpu[-1][2]),'NOT_READY':2,'Game_Main_called':False,'Battle_Main_called':False,
        'stable_30_fps_on_Xbox_verified':False,'audible_audio_verified':False,'complete_game':False,
        'harness':json.loads((a.run/'harness.json').read_text()),
        'visual_review':'Inspect included captures separately; this parser does not certify pixels or complete gameplay'}
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
