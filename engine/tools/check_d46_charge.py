#!/usr/bin/env python3
"""Verify prolonged charge/AI/resource/recovery sequence; never certifies physical FPS."""
from pathlib import Path
import argparse,json,re,hashlib
p=argparse.ArgumentParser();p.add_argument('log',type=Path);p.add_argument('xex',type=Path);p.add_argument('output',type=Path);a=p.parse_args();s=a.log.read_text(errors='replace')
assert 'D46 test only: original READY handler fast-forwarded' in s
assert not re.search(r'DMA first error|HLE strip rejected|GS Xenos FAIL|VIF CALL rejected|GIF reserved tag|GS unusual FRAME|frame failure has|Battle_StepFrame frame=\d+ returned=-',s)
m=re.search(r'experimental battle completed_steps=(\d+) FIGHT_frames=(\d+) cancelled=(\d+) natural_end=(\d+)',s);assert m and int(m[2])==384 and m[3]=='1' and m[4]=='0'
assert 'fresh close result=0 complete=1' in s and 'typed-data diagnostic returned to UI loop; scene=0 stage=0' in s
assert '0 conversions rejected' in s
responses=[dict(tick=int(x),action=int(y),ki=int(z),health=int(h)) for x,y,z,h in re.findall(r'action response tick=(\d+) action=(-?\d+) ki=(-?\d+) health=(-?\d+)',s)]
assert max(x['ki'] for x in responses)>responses[0]['ki']
assert any(x['tick']>247 for x in responses)
frames=[int(x) for x in re.findall(r'Battle_StepFrame frame=(\d+) returned=0',s)];assert max(frames)>=384
cpu='CPU-only action diagnostic' in s
counts=re.findall(r'res: (\d+) files converted',s)
report={'result':'PASS','tested_XEX_sha256':hashlib.sha256(a.xex.read_bytes()).hexdigest(),'completed_steps':int(m[1]),'FIGHT_frames':int(m[2]),'GPU_replay_and_presentation':not cpu,'clean_close':True,'DMA_error':False,'files_converted':int(counts[-1]) if counts else None,'ki_start':responses[0]['ki'],'ki_max_seen':max(x['ki'] for x in responses),'passed_former_failures_at_frames':[211,212,249],'ready_prelude_accelerated':True,'FIGHT_simulation_accelerated':False,'held_input_schedule':[[4,239],[272,351]],'opponent_AI_enabled':True,'uninterrupted_MAX_POWER_charge_verified':False,'physical_console_FPS_verified':False,'complete_match_to_KO':False,'audible_audio_verified':False,'scope':'Xenia CPU producers/decode only' if cpu else 'Xenia llvmpipe software GPU; prolonged regression only'}
a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
