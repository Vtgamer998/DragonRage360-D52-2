#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Synthetic format/reader tests; no ISO, PPC execution or gameplay claim."""
from pathlib import Path
import argparse,ctypes as C,struct,subprocess,sys,json,os
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
e=Path(__file__).resolve().parents[1];project=e.parent;up=a.upstream.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
native=e/'platform/platform';inc=['-I'+str(native),'-I'+str(project/'src')]
sources=[native/'xb_battle_common.c',*[project/'src'/(s+'.c') for s in ('dr_container','dr_disc','dr_anim','dr_bpe','dr_skeleton')]]
lib=out/'common.so';subprocess.run(['gcc','-std=c17','-Wall','-Wextra','-Werror','-shared','-fPIC',*inc,*map(str,sources),'-o',str(lib)],check=True)
f=C.CDLL(str(lib)).XbBattleCommon_ConvertItems;f.argtypes=[C.c_void_p,C.c_size_t,C.c_int,C.c_void_p,C.c_size_t];f.restype=C.c_int
# Full character capacity, every numeric field exercised; distinctive opaque bytes.
a0=64;a1=9728;n=a1+4*40;b=bytearray(n);struct.pack_into('<4I',b,0,2,a0,a1,n)
for i in range(161):
 at=a0+i*60;struct.pack_into('<2I4H7I',b,at,i%32,0x12345678,0x2345,2,i+1,0x3456,*range(1000,1007))
 b[at+44:at+48]=bytes([255,1,2,3]);struct.pack_into('<2f',b,at+48,1.25,-2.5);b[at+56:at+60]=b'ABCD'
for i in range(4):
 at=a1+i*40;b[at:at+12]=bytes([i,4,5,6,7,8,9,10,11,12,13,14]);struct.pack_into('<4h5I',b,at+12,-2,3,-4,5,0x76543210,1,2,4,8)
(out/'common4.bin').write_bytes(b)
src=C.create_string_buffer(bytes(b));dst=C.create_string_buffer(n)
fields=[(i,4) for i in range(0,16,4)]
for at in range(a0,a0+161*60,60):fields += [(at+j,4) for j in [0,4,*range(16,44,4),48,52]]+[(at+j,2) for j in range(8,16,2)]
for at in range(a1,n,40):fields += [(at+j,2) for j in range(12,20,2)]+[(at+j,4) for j in range(20,40,4)]
gold=bytearray(b)
for at,width in fields:gold[at:at+width]=gold[at:at+width][::-1]
assert f(src,n,1,dst,n)==0 and dst.raw==gold
assert f(src,n,0,dst,n)==0 and dst.raw==b
bad=[]
for at,data in [(0,struct.pack('<I',3)),(4,struct.pack('<I',0)),(8,struct.pack('<I',n+4)),(12,struct.pack('<I',n+4)),(a0,struct.pack('<I',32)),(a0+10,b'\0\0'),(a0+44,b'\xa1'),(a0+48,struct.pack('<I',0x7fc00000)),(a1,b'\x04'),(a0+161*60,b'\x01')]:
 q=bytearray(b);q[at:at+len(data)]=data;bad.append(q)
for q in bad:
 dst.raw=b'\xa5'*n;assert f(C.create_string_buffer(bytes(q)),n,1,dst,n)<0 and dst.raw==b'\xa5'*n
assert f(src,n,1,dst,n-1)<0 and f(src,n,1,src,n)<0
# Canonical collision with one upward-facing solid triangle at y=10.
c=bytearray(316)
def w(at,*v):struct.pack_into('<'+'I'*len(v),c,at,*v)
def fl(at,*v):struct.pack_into('<'+'f'*len(v),c,at,*v)
w(56,1,24,76,0);w(100,40);fl(128,0,0,0,1,100,1000,100,1)
w(160,1,0,0,48,56,64);fl(192,-100,10,-100,100,10,100);w(216,0,0xffffffff)
w(224,0,0,1,2);fl(240,0,-1,0,-10);fl(256,-100,10,-100,1,100,10,-100,1,0,10,100,1);fl(304,1000,-1000,1000)
m=bytearray(296);struct.pack_into('<2I',m,0,0x4450414d,0)
for at,count,offset in [(40,2,168),(48,1,232),(56,1,264)]:struct.pack_into('<2I',m,at,count,offset)
for offset,x,target in [(168,-10,10),(200,10,-10),(232,0,10),(264,5,-10)]:struct.pack_into('<8f',m,offset,x,0,0,1,target,0,0,1)
pack=struct.pack('<4I',2,16,16+len(c),16+len(c)+len(m))+c+m
# Pack members must start at a multiple of four (both lengths satisfy this).
fixture=out/'stage.bin';fixture.write_bytes(pack)
sys.path.insert(0,str(up/'port/tools'));import portsrc
sys.path.insert(0,str(e/'tools'));from fixed_profile_source import transform
original=[]
for stem in ('col_a','eft_det_b','eft_det_a'):
 text,_,_=portsrc.enable_attempts((up/'src/battle'/(stem+'.c')).read_text());dest=out/(stem+'.c');dest.write_text(text);original.append(dest)
text,_,_=portsrc.enable_attempts((up/'src/battle/stg_a_b.c').read_text());patched,_=transform('src/battle/stg_a_b.c',text)
assert patched.count('if (XbStage_TryStartPlace(')==2 and patched.count('!gBtlStage || !BtlStage_IsReady() || !gBtlStage->data')==2
(out/'stg_a_b.c').write_text(patched)
flags=['-std=gnu99','-O1','-g','-fcommon','-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-omit-frame-pointer',*inc,'-I'+str(up/'include'),'-I'+str(up/'port/include')]
objects=[];logs=[]
for i,s in enumerate([e/'tests/d9/test_spawn.c',native/'xb_battle_stage.c',*original,*[project/'src'/(x+'.c') for x in ('dr_stage_collision','dr_container','dr_disc','dr_anim','dr_bpe','dr_skeleton')]]):
 obj=out/(str(i)+'.o');extra=['-include',str(up/'port/include/port_compat.h')] if s in original else []
 r=subprocess.run(['gcc',*flags,*extra,'-c',str(s),'-o',str(obj)],capture_output=True,text=True);logs.append(r.stdout+r.stderr);(out/'compile.txt').write_text(''.join(logs));r.check_returncode();objects.append(obj)
r=subprocess.run(['gcc','-fsanitize=address,undefined',*map(str,objects),'-Wl,--gc-sections','-lm','-o',str(out/'spawn')],capture_output=True,text=True);logs.append(r.stdout+r.stderr);(out/'compile.txt').write_text(''.join(logs));r.check_returncode()
r=subprocess.run([str(out/'spawn'),str(fixture)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'spawn.txt').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);r.check_returncode()
report={'result':'PASS','synthetic_fixtures_only':True,'common4_endian_oracle':len(fields),'common4_corruptions_rejected':len(bad),'original_ground_triangle_and_collision_executed':True,'six_spawn_queries':True,'missing_ground_refused':True,'MAPD_corruptions_rejected':3,'native_dispatch_and_uninstall_tested':True,'original_GetStartPlace_dispatch_source_checked':True,'PowerPC_execution':False,'gameplay':False}
(out/'tests.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
