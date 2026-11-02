#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
from pathlib import Path
import argparse,os,subprocess
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--sanitize',action='store_true');a=p.parse_args()
root=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
cmd=['gcc','-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-ffp-contract=off','-DXB_HOST_TEST',
     '-I'+str(root/'src'),'-I'+str(root/'engine/platform/platform')]
if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
cmd += [str(root/n) for n in ['engine/tests/test_dma_mapped.c','src/dr_dma_producer.c','src/dr_gs_stream.c','src/dr_vu1.c',
                             'engine/platform/platform/xb_iop.c','engine/platform/platform/xb_resconv.c']]
exe=out/'mapped-test';subprocess.run([*cmd,'-lm','-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
(out/'mapped.txt').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
