#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Load three explicitly typed real game tables with actual xb_gamedata.c.
Writes only registered host test buffers, never the game executable or XEX.
"""
from pathlib import Path
import argparse,ctypes as C,hashlib,json,struct,subprocess
p=argparse.ArgumentParser(description=__doc__);p.add_argument('disc',type=Path);p.add_argument('output',type=Path)
a=p.parse_args();e=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
sources={'SLUS_216.78':(a.disc/'SLUS_216.78').read_bytes(),'DBZP.BIN':(a.disc/'BIN/DBZP.BIN').read_bytes()}
elf=sources['SLUS_216.78'];shoff=struct.unpack_from('<I',elf,32)[0];shsize,n=struct.unpack_from('<HH',elf,46)
def original(address,length):
 for i in range(n):
  _,kind,flags,addr,offset,size=struct.unpack_from('<6I',elf,shoff+i*shsize)
  if flags&2 and kind!=8 and addr<=address and length<=size-(address-addr):return elf[offset+address-addr:offset+address-addr+length]
 raise ValueError('source address outside ELF sections')
# Offsets are the pinned table labels in port/data/cod_1C3400.data.s.
# Widths are explicit C declarations in mathf.c, snd.h and battle_setup.h.
rows=[('gMathfSinCoef',0x2c3450,4,4,0x10),('gSndBankVolume',0x2c3460,4,8,0x30),('gBattleTimeLimitTbl',0x2c3480,2,8,0x60)]
subprocess.run(['gcc','-std=gnu17','-O2','-Wall','-Wextra','-Werror','-fPIC','-shared','-DXB_HOST_TEST','-I'+str(e/'platform/platform'),str(e/'platform/platform/xb_gamedata.c'),'-o',str(out/'gamedata.so')],check=True)
lib=C.CDLL(str(out/'gamedata.so'));libc=C.CDLL(None);libc.malloc.argtypes=[C.c_size_t];libc.malloc.restype=C.c_void_p
Read=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_char_p,C.POINTER(C.c_void_p),C.POINTER(C.c_size_t))
class Files(C.Structure):_fields_=[('read',Read),('ctx',C.c_void_p)]
class Target(C.Structure):_fields_=[('address',C.c_uint32),('bytes',C.c_void_p),('size',C.c_size_t)]
@Read
def read(ctx,name,pointer,size):
 data=sources[name.decode()];dest=libc.malloc(len(data))
 if not dest:return -1
 C.memmove(dest,data,len(data));pointer[0]=dest;size[0]=len(data);return 0
lib.XbGameData_SetTargets.argtypes=[C.POINTER(Target),C.c_size_t]
lib.XbGameData_Load.argtypes=[C.POINTER(Files),C.c_void_p,C.c_size_t,C.c_long,C.c_int]
memory=C.create_string_buffer(256);target=Target(0x82001000,C.cast(memory,C.c_void_p),256);files=Files(read,None)
assert not lib.XbGameData_SetTargets(C.byref(target),1)
records=[];hash_value=0xcbf29ce484222325;expected={}
for name,address,width,count,dest in rows:
 raw=original(address,width*count);expected[name]=raw
 for byte in raw:hash_value=((hash_value^byte)*0x100000001b3)&0xffffffffffffffff
 records.append((0x82001000+dest,0,address-0x100000,len(raw),width))
manifest=struct.pack('<4sIQ',b'BT3X',len(records),hash_value)+b''.join(struct.pack('<5I',*r) for r in records)
buf=C.create_string_buffer(manifest);assert lib.XbGameData_Load(C.byref(files),buf,len(manifest),0,0)==0
for name,address,width,count,dest in rows:
 fmt='I' if width==4 else 'h'
 assert struct.unpack('<'+fmt*count,expected[name])==struct.unpack_from('>'+fmt*count,memory.raw,dest),name
before=memory.raw;bad=bytearray(manifest);bad[8]^=1;buf=C.create_string_buffer(bytes(bad))
assert lib.XbGameData_Load(C.byref(files),buf,len(bad),0,0)==5 and memory.raw==before
assert not lib.XbGameData_ClearTargets()
report={'result':'PASS','source_program_sha256':{name:hashlib.sha256(data).hexdigest() for name,data in sources.items()},'tables':[{'name':name,'source_address':hex(address),'width':width,'count':count} for name,address,width,count,dest in rows],'real_source_coverage_checked':True,'checksum_failure_preserves_destination':True,'destination':'host test buffer only','game_global_tables_initialized':False,'PowerPC_execution':False,'battle_execution':False}
(out/'gamedata-real.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: three real scalar tables -> actual loader -> registered BE host buffers; bad checksum leaves destinations unchanged; no battle execution')
