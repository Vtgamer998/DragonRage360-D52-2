#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Execute the shared native DMA/VIF/GIF session on the host, no GPU/game."""
from pathlib import Path
import argparse,json,subprocess
p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path);p.add_argument('--sanitize',action='store_true')
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);engine=Path(__file__).resolve().parents[1]
cmd=['gcc','-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-ffp-contract=off','-I'+str(engine/'graphics'),'-I'+str(engine/'vu')]
if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
cmd+=list(map(str,[engine/'tests/test_gs_native.c',engine/'graphics/dr_gs_native.c',engine/'graphics/dr_gs_stream.c',engine/'vu/dr_vu1.c']))
cmd+=['-lm','-o',str(out/'native-session')];subprocess.run(cmd,check=True)
r=subprocess.run([str(out/'native-session')],capture_output=True,text=True);(out/'native-session.txt').write_text(r.stdout+r.stderr)
print(r.stdout+r.stderr,end='');r.check_returncode()
(out/'native-session.json').write_text(json.dumps({'result':'PASS','sanitizers':a.sanitize,'PPC_execution':False,'GPU_execution':False,'battle_execution':False,'scope':'shared native VU session, bound canonical mappings, direct GIF and all DMA chain types, sticky errors, lifecycle and re-entry'},indent=2)+'\n')
