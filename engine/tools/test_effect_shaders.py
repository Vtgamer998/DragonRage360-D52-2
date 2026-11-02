#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Compile/run native effect microcode on CPU; no rasterizer or hardware evidence.
Usage: test_effect_shaders.py <xsdk> <glslang> <out>
"""
from pathlib import Path
import itertools,json,math,struct,subprocess,sys
engine=Path(__file__).resolve().parents[1]
sdk,glslang,out=(Path(p).resolve() for p in sys.argv[1:4]);out.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(sdk/'xenos-toolchain'))
from xsc import native
from xdis import simulate
from xasm.program import XSDK_REGISTERS

def shader(name):
 spv=out/(name+'.spv');subprocess.run([str(glslang),'-V',str(engine/'graphics/shaders'/name),'-o',str(spv)],check=True,stdout=subprocess.DEVNULL)
 blob,manifest,resources=native.compile_spirv(spv.read_bytes(),XSDK_REGISTERS.pixel,XSDK_REGISTERS.vertex,optimize=True)
 (out/(name+'.native')).write_bytes(blob)
 return blob,{reg:list(value) for reg,value in manifest if isinstance(reg,int)},resources

def f32(x):return struct.unpack('<f',struct.pack('<f',x))[0]
def near(got,want,tag):assert max(abs(a-b) for a,b in zip(got,want))<4e-6,(tag,got,want)

def table_colour(index,alpha):return [(index*37+11)&255,(index*13+29)&255,(index*3+73)&255,alpha]
blob,literals,resources=shader('dr_dclut.frag');assert ('push',0,0,0,2) in resources
clut_cases=0
for source,maximum,alpha,prescale in itertools.product([0,1],[65535,16777215,4294967295],[0,64,127,128,255],[0,1]):
 values=[0,1,255,256,257,65447,65448,65449,65535] if source==0 else range(256)
 for value in values:
  normal=f32(value/f32(maximum)) if source==0 else 0.25
  z=f32(normal*f32(maximum));index=(0 if z>=65448 else max(0,min(255,255-int(z/256)))) if source==0 else value
  constants=dict(literals);constants[0]=[source,maximum,1/1280,1/1024];constants[1]=[1/1024,1/1024,prescale,0];constants[255]=[0,0,0,0]
  fetched=[]
  def fetch(slot,uv):
   if slot==0:return [normal,0,0,1]
   if slot==1:return [value/255 if source else 0.5,0,0,1]
   assert slot==2 and abs(uv[1]-0.5)<1e-6
   actual=int(uv[0]*256);assert actual==index,(source,maximum,value,actual,index,uv)
   fetched.append(actual);return [c/255 for c in table_colour(actual,alpha)]
  result=simulate.run(blob,{0:[16.5,32.5,0,0]},constants,texture_fetch=fetch)
  assert not result.killed and fetched==[index]
  c=table_colour(index,alpha);a=alpha/128
  want=[min(1,c[i]/255*(a if prescale else 1)) for i in range(3)]+[min(1,a)]
  near(result.exports[0],want,('clut',source,maximum,value,alpha,prescale))
  near(result.exports[1],[alpha/255,0,0,1],('aux',alpha));clut_cases+=1

blob,literals,resources=shader('dr_outline.frag');assert ('push',0,0,0,2) in resources
outline_cases=0
for here_id,side in itertools.product(range(256),range(5)):
 # One of four neighbours is the stage ID, or all equal (side==4).
 neighbours=[here_id]*4
 if side<4:neighbours[side]=255
 def value(i):
  if i==255:return 0
  c=(i<<3)&255
  return c if c else 128
 expected=100/255 if any(value(here_id)>value(i) for i in neighbours) else 0
 constants=dict(literals);constants[0]=[1/1280,1/1024,100/255,0];constants[1]=[0.5/1280,0.5/1024,1023.5/1280,1023.5/1024];constants[255]=[0,0,0,0]
 ids={(16,32):here_id,(17,32):neighbours[0],(15,32):neighbours[1],(16,33):neighbours[2],(16,31):neighbours[3]};seen=[]
 def fetch(slot,uv):
  assert slot==0
  pos=(int(uv[0]*1280),int(uv[1]*1024));assert pos in ids,pos
  seen.append(pos);return [ids[pos]/255,0,0,1]
 result=simulate.run(blob,{0:[16.5,32.5,0,0]},constants,texture_fetch=fetch)
 assert not result.killed and len(seen)==5
 near(result.exports[0],[expected]*3+[0],('outline',here_id,side));outline_cases+=1
# The snapshot is wider than the valid target: neighbours must clamp to its
# copied rectangle, not to the allocation's 1280-pixel right edge.
edge_cases=0
for x,y in itertools.product([0,1023],[0,1023]):
 seen=[]
 def fetch_edge(slot,uv):
  pos=(int(uv[0]*1280),int(uv[1]*1024));assert 0<=pos[0]<1024 and 0<=pos[1]<1024,pos
  seen.append(pos);return [1/255,0,0,1]
 result=simulate.run(blob,{0:[x+0.5,y+0.5,0,0]},constants,texture_fetch=fetch_edge)
 assert len(seen)==5;near(result.exports[0],[0]*4,('corner',x,y));edge_cases+=1
report={'result':'PASS','CLUT_microcode_cases':clut_cases,'outline_microcode_cases':outline_cases,'snapshot_boundary_cases':edge_cases,'scope':'original depth/ID lookup, alpha scaling/raw-byte output, source premultiplication, outline table/4-neighbours and copied-region clamp','GPU_execution':False,'rasterization':False,'hardware_pixels':False}
(out/'effects-shaders.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
