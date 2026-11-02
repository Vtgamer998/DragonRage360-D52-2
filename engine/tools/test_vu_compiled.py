#!/usr/bin/env python3
"""Validate shipped Xenos VU shader containers against independent scalar math.
Usage: test_vu_compiled.py <xsdk> <out>. This is NOT a GPU/pixel test.
"""
from pathlib import Path
import itertools,json,math,struct,subprocess,sys
engine=Path(__file__).resolve().parents[1]
sdk,out=(Path(p).resolve() for p in sys.argv[1:3]);out.mkdir(parents=True,exist_ok=True)
sys.path[:0]=[str(sdk/'xenos-toolchain'),str(sdk/'xenos-toolchain/tools')]
from xdis import simulate
from xdis import container

# Vertex fetch patching follows xsdk tools/check_hlsl.py, MIT, Mena,
# revision 8c58c7a3fb37bb261c5a11a8a1f4fa8a8bfd0d28.
def vertex_elements(blob):
 words=struct.unpack('>'+str(len(blob)//4)+'I',blob);at=words[6]//4
 return [(w&0xfff,w>>12&15,w>>16&15) for w in words[at+10:at+10+words[at+7]]]
def separated(blob,elements):
 c=container.Container(blob);out=bytearray(blob)
 for n,(slot,_,_) in enumerate(elements):
  at=c.offset+c.sh_off+slot*12;word=struct.unpack_from('>I',out,at)[0]&~(0x7f<<20)
  struct.pack_into('>I',out,at,word|(n//3)<<20|(n%3)<<25)
 return bytes(out)
def compile_shader(name):
 import re
 key=name.replace('.', '_')
 header=(engine.parent/'src'/('dr_gs_shaders.h' if key=='dr_gs_frag' else 'dr_vu_shaders.h')).read_text()
 words=re.search(r'static const uint32_t '+key+r'_container\[\] = \{(.*?)\};',header,re.S)[1]
 values=[int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]+',words)]
 blob=struct.pack('>'+str(len(values))+'I',*values)
 lits=re.search(r'static const XsdkShaderLiteral '+key+r'_literals\[\] = \{(.*?)\};',header,re.S)[1]
 literals={}
 for reg,vec in re.findall(r'\{\s*(\d+),\s*\{([^}]+)\}',lits):literals[int(reg)]=[float(x.strip().rstrip('f')) for x in vec.split(',')]
 resources=[]
 if name.endswith('.vert'):
  reg=int(re.search(key.upper()+r'_UNIFORM_1_0 = (\d+)',header)[1]);resources=[('uniform',1,0,reg,19)]
 return blob,literals,resources

def mul(m,p):return [sum(m[c*4+row]*p[c] for c in range(4)) for row in range(4)]
def matrix(tx=0,ty=0,tz=0):return [1,0,0,0,0,1,0,0,0,0,1,0,tx,ty,tz,1]
def close(got,want,tag,tol=2e-5):
 assert len(got)>=len(want) and all(math.isfinite(x) for x in got[:len(want)]), (tag,got,want)
 assert max(abs(a-b) for a,b in zip(got,want))<tol,(tag,got,want)

cases=0
for program,name in enumerate(['dr_vu0.vert','dr_vu4.vert','dr_vu6.vert'],1):
 blob,literals,resources=compile_shader(name);elements=vertex_elements(blob);patched=separated(blob,elements)
 ureg=next(r for k,s,b,r,n in resources if (k,s,b)==('uniform',1,0))
 for layer,weight,negative,steps in itertools.product(range(6) if program==1 else [0],[0,0.25,0.75,1],[False,True],[65535.0,16777215.0]):
  u=[0.0]*76;u[:16]=matrix(3,-4,2);u[16:32]=matrix(-2,5,-3)
  u[0]=0;u[1]=1;u[4]=-1;u[5]=0;u[10]=0.5
  u[16]=0.5;u[21]=2;u[26]=1.5
  u[32:36]=[1,-2,3,0];u[36:40]=[-2,1,-1,0]
  # Perspective and unequal bone translations expose transpose/weight mistakes.
  u[40:56]=[1,0,0,0.03125,0,1,0,0,0,0,1,0,512,512,3000.375,-2 if negative else 2]
  u[56:60]=[0.25,-0.5,0.75,0.125];u[60:64]=[64,128,192,128];u[64:68]=[32,96,160,64]
  u[68:72]=[16,32,steps,float(layer)];u[72:76]=[-0.25,0.5,0.125,0.25]
  pos=[4,-3,2,weight];normal=[0.5,-0.25,0.75,96.75];st=[0.25,0.75,1,0]
  if program!=1:normal=[64.75,128.25,192.9,96.75]
  p=pos[:] if program==2 else pos[:3]+[1]
  if program==1:
   w=1 if layer==4 else weight
   pa=mul(u[:16],[pos[i]-u[32+i] for i in range(3)]+[1]);pb=mul(u[16:32],[pos[i]-u[36+i] for i in range(3)]+[1])
   p=[pb[i]*(1-w)+pa[i]*w for i in range(3)]+[1]
  s=mul(u[40:56],p)
  if program==1 and layer==2 and s[3]<0:s=[-x for x in s]
  z=s[2]/steps
  if s[3]>0:z=(math.floor(s[2]/s[3])+0.5)/steps*s[3]
  if program==1 and layer==2:z=min(max(s[2]/steps,0),s[3])
  position=[(s[0]-u[68]*s[3])/512-s[3],s[3]-(s[1]-u[69]*s[3])/512,z,s[3]]
  if program==1:
   colour=[x/128 for x in u[60:64]];uv=st[:2]+[1]
   if layer==5:
    colour=[x/128 for x in u[64:68]];n=normal[:3]+[1]
    uv=[sum(a*b for a,b in zip(u[56:60],n))*0.5+0.5,sum(a*b for a,b in zip(u[72:76],n))*0.5+0.5,1]
   elif layer>=3:
    raw=normal[:3]+[u[63] if layer==4 else normal[3]];colour=[min(255,max(0,math.floor(x)))/128 for x in raw]
   elif layer==1:
    na=mul(u[:16],normal[:3]+[0]);nb=mul(u[16:32],normal[:3]+[0]);shade=sum(u[56+i]*(nb[i]*(1-weight)+na[i]*weight) for i in range(3))
    colour=[x/128 for x in u[64:68]];uv=[(shade+u[59])*0.5+0.5,0,1]
  else:
   colour=[min(255,max(0,math.floor(x)))/128 for x in normal];uv=st[:2]+[1]
   if program==3:
    shadow=mul(u[:16],pos[:3]+[1]);uv=[(shadow[i]*u[32]+1)*0.5*u[36+i] for i in range(2)]+[1]
  constants=dict(literals);constants.update({ureg+i:u[i*4:i*4+4] for i in range(19)})
  inputs={(0,0):pos,(10,0):normal,(5,0):st};data=[inputs[(usage,index)] for _,usage,index in elements]
  result=simulate.run(patched,{0:[0]*4},constants,vertex_fetch=lambda n,f,i:data[f.fetch_constant_index()])
  tag=(name,layer,weight,negative,steps)
  close(result.exports[62],position,tag);close(result.exports[0],colour,tag);close(result.exports[1],uv,tag);close(result.exports[2],position[2:4],tag)
  cases+=1

# Quantized fragment depth comes from interpolated (clip.z, clip.w), not FragCoord.z.
blob,literals,resources=compile_shader('dr_gs.frag');depth_cases=0
for z,w,steps in itertools.product([0.0,0.125,0.50001,0.75],[0.5,1,2],[0,65535.0,16777215.0]):
 if z/w>1:continue
 constants=dict(literals);constants[0]=[0,0,1,0];constants[1]=[0,0,0,0];constants[2]=[1/1280,1/1024,steps,0];constants[3]=[0,0,1,1]
 result=simulate.run(blob,{0:[1]*4,1:[0,0,1,0],2:[z,w,0,0],3:[16,32,0,0]},constants,texture_fetch=lambda n,uv:[1,1,1,128/255])
 depth=z/w;want=math.floor(depth*steps)/steps if steps else depth
 close(result.exports[61],[want],('depth',z,w,steps),2e-7);depth_cases+=1
report={'result':'PASS','vertex_microcode_cases':cases,'fragment_depth_cases':depth_cases,'programs':['model skinning / toon / layer variants','stage','shadow projection'],'GPU_execution':False,'input':'exact shipped native shader containers; no shader recompilation','pixels_verified':False,'battle_simulation':False,'reference':'independent scalar matrix, lighting and projection equations from upstream shaders'}
(out/'vu-shaders.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
