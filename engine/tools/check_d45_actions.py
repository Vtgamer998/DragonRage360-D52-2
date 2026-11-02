#!/usr/bin/env python3
"""Check the bounded D45 scripted-action probe; does not certify gameplay or FPS."""
from pathlib import Path
import argparse,json,re
p=argparse.ArgumentParser();p.add_argument('log',type=Path);p.add_argument('output',type=Path);a=p.parse_args();s=a.log.read_text(errors='replace')
assert 'D45 test only: original READY handler fast-forwarded' in s
for tick,buttons,lt in [(4,'4000',0),(16,'0000',255),(36,'2000',0),(48,'8000',0)]:
 assert f'D45 scripted action tick={tick} buttons={buttons} LT={lt}' in s,(tick,'missing action')
assert not re.search(r'DMA first error|OT .+ rejected|frame failure has|Battle_StepFrame frame=\d+ returned=-',s)
m=re.search(r'experimental battle completed_steps=(\d+) FIGHT_frames=(\d+) cancelled=(\d+) natural_end=(\d+)',s);assert m and int(m[2])==64 and m[3]=='1' and m[4]=='0'
assert 'fresh close result=0 complete=1' in s and 'typed-data diagnostic returned to UI loop' in s
assert '0 conversions rejected' in s
responses=[dict(tick=int(x),action=int(y),ki=int(z),health=int(h)) for x,y,z,h in re.findall(r'action response tick=(\d+) action=(-?\d+) ki=(-?\d+) health=(-?\d+)',s)]
assert len(responses)==6
assert responses[2]['ki']>responses[0]['ki'], 'charge did not increase ki'
assert responses[1]['action']!=responses[0]['action'], 'attack did not change action'
assert responses[4]['action']!=responses[3]['action'], 'B did not change action'
cpu='D45 CPU-only action diagnostic' in s
report={'result':'PASS','GPU_replay_and_presentation':not cpu,'completed_steps':int(m[1]),'FIGHT_frames':int(m[2]),'four_input_sequences_submitted':True,'responses':responses,'DMA_error':False,'clean_close':True,'ready_prelude_accelerated':True,'battle_simulation_accelerated':False,'complete_match_to_KO':False,'physical_console_FPS_verified':False,'audible_audio_verified':False,'scope':'Xenia CPU producers/decode only; no GPU replay' if cpu else 'Xenia llvmpipe; bounded action/cleanup regression only'}
a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
