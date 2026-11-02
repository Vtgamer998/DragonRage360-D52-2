#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise actual game heap + native arena using host low-address allocations."""
from pathlib import Path
import argparse,subprocess,os,json
p=argparse.ArgumentParser(description=__doc__);p.add_argument('upstream',type=Path);p.add_argument('xsdk',type=Path);p.add_argument('output',type=Path);p.add_argument('--sanitize',action='store_true')
a=p.parse_args();up=a.upstream.resolve();sdk=a.xsdk.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);e=Path(__file__).resolve().parents[1];plat=e/'platform/platform'
common=['gcc','-std=gnu17','-O1','-g','-fno-builtin','-ffunction-sections','-fdata-sections','-I'+str(up/'include'),'-I'+str(plat),'-I'+str(sdk/'include')]
if a.sanitize:common+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
rows=[(e/'tests/test_heap_native.c',[]),(e/'combat/heap.c',['-DPORT_BIGENDIAN','-include',str(up/'port/include/port_compat.h'),'-Wno-pointer-to-int-cast','-Wno-int-to-pointer-cast']),
      (plat/'xb_plat_mem.c',['-Dcalloc=XbTest_Calloc','-Dfree=XbTest_Free']),
      (plat/'xb_resconv.c',['-DXB_HOST_TEST']),(plat/'xb_game_profile.c',[]),(up/'src/sys/align.c',['-include',str(up/'port/include/port_compat.h')])]
objects=[]
for i,(src,flags) in enumerate(rows):
 obj=out/(str(i)+'.o');objects.append(obj);subprocess.run([*common,*flags,'-c',str(src),'-o',str(obj)],check=True)
exe=out/'test-heap';subprocess.run([*common,'-Wl,--gc-sections',*map(str,objects),'-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
(out/'heap.txt').write_text(r.stdout+r.stderr);print(r.stdout,end='')
if r.returncode:print(r.stderr[-3000:])
r.check_returncode();(out/'heap.json').write_text(json.dumps({'result':'PASS','sanitizers':a.sanitize,'scope':'actual adapted engine heap + native arena + converter registry; allocator/log transport replaced only on host','randomized_operations':3000,'PowerPC_executed':False,'battle_executed':False},indent=2)+'\n')
