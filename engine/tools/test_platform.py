#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Host tests of the actual resource registry, BPE, converters and ADXF layer."""
from pathlib import Path
import argparse,os,re,subprocess
p=argparse.ArgumentParser();p.add_argument('xsdk',type=Path);p.add_argument('output',type=Path)
p.add_argument('--cc',default='gcc');p.add_argument('--sanitize',action='store_true');a=p.parse_args()
root=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
platform=root/'engine/platform/platform'
common=['-std=gnu99','-O1','-g','-ffunction-sections','-fdata-sections','-I'+str(platform),'-I'+str(root/'src'),'-I'+str(a.xsdk.resolve()/'include')]
if a.sanitize:common+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
sets={
 'pipeline':(['-DXB_HOST_TEST'],[root/'engine/tests/test_xb_pipeline.c',*[platform/n for n in ['xb_resconv.c','xb_resource_map.c','xb_bpe.c','xb_demo_resources.c']],*[root/'src'/n for n in ['dr_bpe.c','dr_anim.c','dr_skeleton.c','dr_texfile.c','dr_gs_texture.c','dr_container.c']]]),
 'files':([],[root/'engine/tests/test_xb_file.c',platform/'xb_plat_file.c',platform/'xb_resconv.c'])}
for name,(flags,sources) in sets.items():
    exe=out/name
    subprocess.run([a.cc,*common,*flags,*map(str,sources),'-Wl,--gc-sections','-o',str(exe)],check=True)
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0') # LSAN cannot inspect /proc in this environment.
    run=subprocess.run([str(exe)],capture_output=True,text=True,env=env)
    (out/(name+'.log')).write_text(run.stdout+run.stderr)
    if run.returncode:raise SystemExit(name+' failed; see '+str(out/(name+'.log')))
    if name=='pipeline':
        ids=re.findall(r'file id (\d+) .*read with no converter',run.stderr)
        assert len(ids)==len(set(ids))==701,'once-per-id logging failed'
    print(run.stdout.strip())
