#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Check the original integer-load/CVT sequence behind the water constant."""
from pathlib import Path
import argparse,struct,json,hashlib
p=argparse.ArgumentParser();p.add_argument('program',type=Path);p.add_argument('output',type=Path);a=p.parse_args();b=a.program.read_bytes();assert hashlib.sha256(b).hexdigest()=='811188ba9b416500d921cd4d9514df0cbf42f3a41a99cf5aac5a3da37171bf99'
def at(addr):
 sh=struct.unpack_from('<I',b,32)[0];step,n=struct.unpack_from('<HH',b,46)
 for i in range(n):
  _,kind,flags,va,o,size=struct.unpack_from('<6I',b,sh+i*step)
  if kind==1 and va<=addr and addr+4<=va+size:return struct.unpack_from('<I',b,o+addr-va)[0]
 raise ValueError('unmapped program address')
w=at(0x108b54);assert w>>26==15 and w>>16&31==2 and w&65535==0x30
w=at(0x108b78);assert w>>26==9 and w>>21&31==2 and w>>16&31==2
imm=w&65535;assert 0x300000+imm-65536==0x2fe8d0
w=at(0x108b80);assert w>>26==0x31 and w>>21&31==2 and w>>16&31==30 and w&65535==0
w=at(0x108b84);assert w>>26==0x11 and w>>21&31==20 and w&63==0x20 and w>>11&31==30 and w>>6&31==30
assert at(0x2fe8d0)==224 and at(0x2fe8d4)==0
a.output.write_text(json.dumps({'result':'PASS','source':'USA executable, exact SHA256 checked','address':'D_002FE8D0','reader':['0x108B54 LUI v0,0x30','0x108B78 ADDIU v0,v0,-0x1730','0x108B80 LWC1 f30,0(v0)','0x108B84 CVT.S.W f30,f30'],'type':'32-bit signed integer loaded as bits then converted numerically to float','bytes_read':4,'next_four_bytes':'checked zero padding'},indent=2)+'\n');print('PASS: original integer word -> LWC1 -> CVT.S.W; width 4 plus checked zero padding')
