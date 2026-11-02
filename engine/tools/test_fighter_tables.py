#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Check emitted BE bytes against scalar reads of the real LE resources."""
from pathlib import Path
import argparse, ctypes as C, struct, subprocess, json
p=argparse.ArgumentParser();p.add_argument('models',nargs=2,type=Path);p.add_argument('output',type=Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
source=Path(__file__).resolve().parents[1]/'platform/platform/xb_fighter_tables.c'
lib=a.output.resolve()/'tables.so';subprocess.run(['gcc','-std=c17','-Wall','-Wextra','-Werror','-shared','-fPIC',str(source),'-o',str(lib)],check=True)
f=C.CDLL(str(lib)).XbFighterTable_Convert;f.argtypes=[C.c_uint,C.c_void_p,C.c_size_t,C.c_int,C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)];f.restype=C.c_int
# Independent field inventory, from the pinned native reader definitions.
fields=[([(0,2)]+[(i,4) for i in list(range(4,0x58,4))+list(range(0x5c,0x70,4))+list(range(0x74,0x80,4))+[0xc8]]+[(i,2) for i in [0x70,0x72,0x80,0x82]]),
 [(r*48,4) for r in range(163)]+[(r*48+i,2) for r in range(163) for i in range(4,0x12,2)],
 [(r*52,2) for r in range(13)]+[(r*52+i,4) for r in range(13) for i in list(range(4,0x24,4))+[0x28,0x2c]],[(i,4) for i in range(0,160,4)]]
fields += [[(i,4) for i in list(range(0,0x18,4))+list(range(0x24,0x30,4))+list(range(0x54,0x78,4))+list(range(0x16c,0x208,4))+list(range(0x22c,0x244,4))]+[(i,2) for i in list(range(0x18,0x24,2))+list(range(0x208,0x214,2))],
 [(i,4) for i in list(range(0,0x10,4))+list(range(0x18,0x38,4))+list(range(0x74,0x94,4))+list(range(0xa0,0xc8,4))]+[(i,2) for i in range(0x10,0x18,2)],[],[(r*36+i,w) for r in range(10) for i,w in [(x,4) for x in range(4,32,4)]+[(32,2),(34,2)]]]
sizes=[256,7824,676,160,0x244,0xc8,0x200,360];results=[]
for model in a.models:
 b=model.read_bytes();counts=[]
 for kind,entry in enumerate([17,18,19,20,23,24,26,27]):
  lo,hi=struct.unpack_from('<2I',b,4+entry*4);raw=b[lo&~3:hi&~3];src=C.create_string_buffer(raw);dst=C.create_string_buffer(b'\xA5'*9000);n=C.c_size_t(99)
  assert f(kind,src,len(raw),1,dst,9000,C.byref(n))==0 and n.value==sizes[kind]
  actual=dst.raw[:n.value];gold=bytearray(raw[:n.value]);mask=set()
  for off,width in fields[kind]:
   gold[off:off+width]=int.from_bytes(raw[off:off+width],'little').to_bytes(width,'big');mask.update(range(off,off+width))
  assert actual==gold
  assert all(actual[i]==raw[i] for i in range(n.value) if i not in mask)
  assert f(kind,src,len(raw),0,dst,9000,C.byref(n))==0 and dst.raw[:n.value]==raw[:n.value]
  for badsize,badcap in [(sizes[kind]-1,9000),(len(raw),sizes[kind]-1)]:
   dst.raw=b'\xA5'*len(dst);assert f(kind,src,badsize,1,dst,badcap,C.byref(n))<0 and n.value==0 and dst.raw==b'\xA5'*len(dst)
  assert f(kind,src,len(raw),1,src,len(raw),C.byref(n))<0
  if kind==0:
   mutated=bytearray(raw);mutated[0xcc]=1;dst.raw=b'\xA5'*len(dst);bad=C.create_string_buffer(bytes(mutated));assert f(kind,bad,len(raw),1,dst,9000,C.byref(n))==-862 and dst.raw==b'\xA5'*len(dst)
  counts.append({'kind':kind,'typed_bytes':sizes[kind],'source_bytes':len(raw),'scalar_fields_checked':len(fields[kind]),'unconverted_source_tail':len(raw)-sizes[kind]})
 results.append({'model':model.name,'tables':counts})
report={'result':'PASS','real_resource_endian_output':True,'BE_execution':False,'transactional_rejection':True,'tables':results};(a.output/'fighter-tables-test.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
