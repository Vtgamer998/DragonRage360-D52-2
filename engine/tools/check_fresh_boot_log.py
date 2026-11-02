#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Validate D35 fresh-recovery evidence; not proof of combat/GPU/audio."""
import argparse,json,re
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('log',type=Path);p.add_argument('--output',type=Path)
a=p.parse_args();s=a.log.read_text(errors='replace')
assert 'DragonRage360 D35: native diagnostic;' in s,'wrong binary/log version'
assert 'data_stripped=2;' in s,'readiness marker missing/changed'
assert s.count('injecting unregistered ID after original phase4 queued 10 files')==1
assert s.count('PASS: D35 partial phase4 refused expected=-897; session closed; following fresh load must succeed')==1
assert s.count('PASS: D35 actual Battle_Load result=1 objects=2 error=0;')==2
assert s.count('D35 fresh close result=0 complete=1;')==2
assert s.count('D35 fresh close result=-897 complete=0;')==1
assert s.count('phase4=1 complete=1 polls=')==2
assert 'res: 50 files converted, 0 reads with no converter' in s
assert 'res: 0 conversions rejected' in s
assert 'typed-data diagnostic returned to UI loop; scene=0 stage=0' in s
fails=[l for l in s.splitlines() if l.startswith('FAIL:')]
expected=lambda l:l=='FAIL: interface first frame orange tile' or bool(re.fullmatch(r'FAIL: D35 fresh original initial job result=-897 phase4=1 complete=0 polls=\d+; CPU queue; no Battle_Init/Update/Main or GPU',l))
assert all(expected(l) for l in fails),fails
assert len([l for l in fails if 'result=-897' in l])==1
r={'result':'PASS','version':'D35','partial_failure_after_real_phase4_queue':True,'fresh_Battle_Load_successes':2,'objects_per_load':2,'converted_files':50,'format_rejections':0,'NOT_READY':2,'internal_interface_pixel_failed':any('interface first frame' in l for l in fails),'Battle_Init_Update_Main_executed':False,'original_loading_GPU_draw_proven':False,'audible_audio_proven':False,'scope':'native runtime log only; distinguish emulator from physical console using run provenance'}
if a.output:a.output.write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps(r,indent=2))
