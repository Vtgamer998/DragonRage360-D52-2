#!/usr/bin/env python3
"""Validate the native camera converter against all 39 original USA stage packs."""
import ctypes as C,json,struct,subprocess,sys,tempfile
from pathlib import Path
from audit_original_battle_load import Disc,Span,Reader
project=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as tmp,open(sys.argv[1],'rb') as f:
 so=Path(tmp)/'camera.so'
 subprocess.run(['gcc','-std=c17','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-Wall','-Wextra','-Werror','-shared','-fPIC','-I'+str(project/'src'),str(project/'src/dr_disc.c'),str(project/'engine/platform/platform/xb_stage_camera.c'),str(project/'engine/platform/platform/xb_stage_effects.c'),'-o',str(so)],check=True)
 lib=C.CDLL(str(so));lib.dr_mount.argtypes=[C.POINTER(Disc),Reader,C.c_void_p,C.c_uint64];lib.dr_game_file.argtypes=[C.POINTER(Disc),C.c_char_p,C.POINTER(Span)];lib.XbStageCamera_Prepare.argtypes=[C.c_void_p,C.c_void_p,C.c_size_t]
 @Reader
 def read(_,at,dest,n):
  f.seek(at);b=f.read(n)
  if len(b)!=n:return -1
  C.memmove(dest,b,n);return 0
 disc=Disc();assert not lib.dr_mount(C.byref(disc),read,None,Path(sys.argv[1]).stat().st_size)
 rows=[];total=0;rejected=0;effects=[]
 lib.XbStageEffects_PrepareEntry.argtypes=[C.c_uint,C.c_void_p,C.c_void_p,C.c_size_t]
 for stage in range(39):
  span=Span();assert not lib.dr_game_file(C.byref(disc),f'pzs3us1/{0x170+stage:05d}.bin'.encode(),C.byref(span))
  f.seek(span.offset);pack=f.read(span.size);count=struct.unpack_from('<I',pack)[0];assert count>=5
  offsets=struct.unpack_from('<'+str(count+1)+'I',pack,4);cameras=0
  for index in (2,3,4):
   a,b=offsets[index:index+2]
   if a==b:continue
   assert 0<=a<b<=len(pack)
   raw=pack[a:b];src=C.create_string_buffer(raw);dest=C.create_string_buffer(len(raw));rc=lib.XbStageCamera_Prepare(dest,src,len(raw));assert not rc,(stage,index,rc)
   channels=struct.unpack_from('<I',raw,12)[0];assert int.from_bytes(dest.raw[12:16],sys.byteorder)==channels
   for pos,value in [(8,0xffffffff),(12,9),(20,0x7fc00000)]:
    bad=bytearray(raw);struct.pack_into('<I',bad,pos,value);bad=C.create_string_buffer(bytes(bad));C.memset(dest,0xa5,len(raw));assert lib.XbStageCamera_Prepare(dest,bad,len(raw))<0;assert dest.raw==b'\xa5'*len(raw);rejected+=1
   cameras+=1;total+=1
  for index in (6,12,21,22):
   a,b=offsets[index:index+2]
   if a==b:continue
   raw=pack[a:b];src=C.create_string_buffer(raw);dest=C.create_string_buffer(len(raw));rc=lib.XbStageEffects_PrepareEntry(index,dest,src,len(raw))
   assert not rc,(stage,index,rc)
   bad=bytearray(raw);struct.pack_into('<I',bad,16 if index==6 else 0,0 if index==6 else 0x7fc00000);bad=C.create_string_buffer(bytes(bad));assert lib.XbStageEffects_PrepareEntry(index,dest,bad,len(raw))<0
   if index==12:
    mesh_offset=struct.unpack_from('<I',raw,92)[0]
    for pos,value in [(68,5),(80,0xffffffff),(92,0xffffffff),(64+mesh_offset+4,0xffffffff),(64+mesh_offset+8,0xffffffff)]:
     damaged=bytearray(raw);struct.pack_into('<I',damaged,pos,value);damaged=C.create_string_buffer(bytes(damaged));assert lib.XbStageEffects_PrepareEntry(index,dest,damaged,len(raw))<0,(stage,pos)
   effects.append({'stage':stage,'entry':index,'result':rc})
  rows.append({'stage':stage,'cameras':cameras})
 print(json.dumps({'result':'PASS','stages':39,'real_cameras':total,'corruptions_rejected':rejected,'original_consumer_executed':False,'GPU_executed':False,'rows':rows,'effect_entries':effects},indent=2))
