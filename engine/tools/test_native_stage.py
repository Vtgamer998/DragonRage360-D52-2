#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Original StgCol_QueryZone and ColMesh code vs independent original-file walk."""
from pathlib import Path
import argparse,struct,subprocess,os,json,sys
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('stage',type=Path);p.add_argument('output',type=Path);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);up=a.upstream.resolve();e=Path(__file__).resolve().parents[1];project=e.parent
pack=a.stage.read_bytes();begin,end=struct.unpack_from('<II',pack,4);b=pack[begin:end]
def u(at):return struct.unpack_from('<I',b,at)[0]
answers=[]
for broken in (0,1):
 state=0xcbf29ce484222325;count=0
 def add(data):
  global state
  for c in data:state=((state^c)*0x100000001b3)&0xffffffffffffffff
 def walk(mesh,index):
  global count
  node=u(mesh+12)*4+index*32;left,right=struct.unpack_from('<II',b,node+24)
  if right==0xffffffff:
   if left==0xffffffff:return
   poly=u(mesh+16)*4+left*32;add(struct.pack('<I',left));add(b[poly:poly+4])
   for vert in struct.unpack_from('<3I',b,poly+4):v=u(mesh+20)*4+vert*16;add(b[v:v+16])
   add(b[poly+16:poly+32]);count+=1
  else:walk(mesh,left);walk(mesh,right)
 for i in range(u(56)):
  z=u(60)*4+i*64;words=[u(z+4)]
  for j in range(u(z+20)):
   r=u(z+24)*4+j*16
   if bool(u(r))==bool(broken):words.append(u(r+8))
  for w in words:
   if w and u(w*4):walk(w*4,0)
 answers.append((count,state))
source=(e/'tests/test_native_stage.c').read_text()
for name,(n,h) in zip(('WHOLE','BROKEN'),answers):source=source.replace('EXPECTED_'+name+'_N',str(n)).replace('EXPECTED_'+name+'_HASH',hex(h)+'ull')
fixture=out/'fixture.c';fixture.write_text(source)
# Enable the same original C attempts as the native engine build.
sys.path.insert(0,str(up/'port/tools'));import portsrc
original=[]
for stem in ('col_a','eft_det_b'):
 s,_,_=portsrc.enable_attempts((up/'src/battle'/(stem+'.c')).read_text());f=out/(stem+'.c');f.write_text(s);original.append(f)
sources=[fixture,e/'platform/platform/xb_battle_stage.c',e/'platform/platform/xb_stage_camera.c',e/'platform/platform/xb_stage_effects.c',*original,*[project/'src'/(n+'.c') for n in ('dr_stage_collision','dr_container','dr_disc','dr_anim','dr_bpe','dr_skeleton')]]
common=['gcc','-std=gnu99','-O1','-g','-fcommon','-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(e/'platform/platform'),'-I'+str(project/'src'),'-I'+str(up/'include'),'-I'+str(up/'port/include'),'-I'+str(up/'port/src')]
objects=[];logs=[]
for i,src in enumerate(sources):
 obj=out/(str(i)+'.o');flags=['-include',str(up/'port/include/port_compat.h')] if src in original else []
 r=subprocess.run(common+flags+['-c',str(src),'-o',str(obj)],capture_output=True,text=True);logs.append(r.stdout+r.stderr);(out/'compile.txt').write_text(''.join(logs));r.check_returncode();objects.append(obj)
r=subprocess.run(['gcc','-fsanitize=address,undefined',*map(str,objects),'-Wl,--gc-sections','-lm','-o',str(out/'native-stage')],capture_output=True,text=True);logs.append(r.stdout+r.stderr);(out/'compile.txt').write_text(''.join(logs));r.check_returncode()
r=subprocess.run([str(out/'native-stage'),str(a.stage.resolve())],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'native-stage-test.txt').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
(out/'native-stage-test.json').write_text(json.dumps({'result':'PASS','original_collision_readers':True,'native_global_stage_and_mesh_base_installed':True,'owned_arena_survives_source_free':True,'whole_and_broken_reference':answers,'sanitizers':True,'host_only':True,'full_stage_boot_ready':False},indent=2)+'\n')
