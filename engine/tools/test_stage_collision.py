#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Compare native stage-0 collision reads with an independent LE file walk.
User supplies the stage pack; only fingerprints/results enter reports.
"""
from pathlib import Path
import argparse,struct,subprocess,os,json
p=argparse.ArgumentParser();p.add_argument('stage',type=Path);p.add_argument('output',type=Path);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);engine=Path(__file__).resolve().parents[1];project=engine.parent
pack=a.stage.read_bytes();n=struct.unpack_from('<I',pack)[0];offsets=struct.unpack_from('<'+str(n+1)+'I',pack,4);b=pack[offsets[0]:offsets[1]]
def u(at):return struct.unpack_from('<I',b,at)[0]
state=0xcbf29ce484222325;triangles=0;meshes=0

def add(data):
 global state
 for c in data:state=((state^c)*0x100000001b3)&0xffffffffffffffff

def walk(mesh,index,path):
 global triangles
 assert index not in path and len(path)<64
 node=u(mesh+12)*4+index*32;left,right=struct.unpack_from('<II',b,node+24)
 if right==0xffffffff:
  if left==0xffffffff:return
  poly=u(mesh+16)*4+left*32;add(struct.pack('<I',left));add(b[poly:poly+4])
  for vert in struct.unpack_from('<3I',b,poly+4):
   v=u(mesh+20)*4+vert*16;assert v+16<=len(b);add(b[v:v+16])
  add(b[poly+16:poly+32]);triangles+=1
 else:
  walk(mesh,left,path+(index,));walk(mesh,right,path+(index,))
for i in range(u(56)):
 zone=u(60)*4+i*64
 words=[u(zone+4)]+[u(u(zone+24)*4+j*16+8) for j in range(u(zone+20))]
 for word in words:
  if word and u(word*4):walk(word*4,0,());meshes+=1
# Compile independent constants only. No model/ISO bytes are embedded.
source=(engine/'tests/test_stage_collision.c').read_text().replace('EXPECTED_HASH',hex(state)+'ull').replace('EXPECTED_TRIANGLES',str(triangles))
fixture=out/'fixture.c';fixture.write_text(source);exe=out/'stage-test'
sources=[fixture,*[project/'src'/(s+'.c') for s in ['dr_stage_collision','dr_container','dr_disc','dr_anim','dr_bpe','dr_skeleton']]]
subprocess.run(['gcc','-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(project/'src'),*map(str,sources),'-lm','-o',str(exe)],check=True)
r=subprocess.run([str(exe),str(a.stage.resolve())],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'stage-test.txt').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
(out/'stage-collision.json').write_text(json.dumps({'result':'PASS','zones':u(56),'mesh_occurrences':meshes,'triangle_occurrences':triangles,'canonical_triangle_fnv64':f'{state:016x}','independent_reference':'Python little-endian file walk vs C native float triangle/AABB accessors','sanitizers':True,'immutable_input':True,'native_engine_installed':False,'Xbox_executed':False},indent=2)+'\n')
