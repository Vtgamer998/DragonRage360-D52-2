#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Independent endian field oracle, corrupt-resource rejection and real binder."""
from pathlib import Path
import argparse,ctypes as C,json,struct,subprocess,sys,os
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('common',type=Path);p.add_argument('output',type=Path);p.add_argument('--i386-root',type=Path);p.add_argument('--qemu',type=Path);a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);up=a.upstream.resolve();engine=Path(__file__).resolve().parents[1];project=engine.parent
native=engine/'platform/platform';sources=[native/'xb_battle_common.c',*[project/'src'/(x+'.c') for x in ('dr_container','dr_disc','dr_anim','dr_bpe','dr_skeleton')]]
includes=['-I'+str(native),'-I'+str(project/'src')];lib=out/'common.so'
subprocess.run(['gcc','-std=c17','-Wall','-Wextra','-Werror','-shared','-fPIC',*includes,*map(str,sources),'-o',str(lib)],check=True)
f=C.CDLL(str(lib)).XbBattleCommon_Convert;f.argtypes=[C.c_uint,C.c_void_p,C.c_size_t,C.c_int,C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)];f.restype=C.c_int
b=a.common.read_bytes();assert len(b)==88832
assert struct.unpack_from('<I',b)[0]==213
headers=struct.unpack_from('<215I',b);results=[]
for kind,word in enumerate([3,5,6,7,8,9,4]):
 raw=b[headers[word]:headers[word+1]];src=C.create_string_buffer(raw);dst=C.create_string_buffer(len(raw));written=C.c_size_t()
 assert f(kind,src,len(raw),1,dst,len(raw),C.byref(written))==0 and written.value==len(raw)
 fields=[]
 if kind in (0,4):fields=[(i,4) for i in range(0,len(raw),4)]
 elif kind==1:fields=[(i+j,w) for i in range(0,len(raw)-35,36) for j,w in [(k,4) for k in range(4,32,4)]+[(32,2),(34,2)]]
 elif kind==2:fields=[(i+j,w) for i in range(0,len(raw)-7,8) for j,w in [(0,2),(2,2),(4,4)]]
 elif kind==3:fields=[(i+j,w) for i in range(0,len(raw)-23,24) for j,w in [(6,2),(8,4),(20,2)]]
 elif kind==6:
  fields=[(i,4) for i in range(0,164,4)];sizes=struct.unpack_from('<41I',raw);at=328
  for i,n in enumerate(sizes):
   if i==0:
    fields += [(at+j,2) for j in [0,2]+list(range(8,264,2))+list(range(648,904,2))]
    fields += [(j+k,w) for j in range(at+1928,at+n,20) for k,w in [(4,2),(6,2),(8,4),(12,4),(16,4)]]
   elif i<9:fields += [(at,2)]+[(at+8+28*j,2) for j in range(struct.unpack_from('<H',raw,at)[0])]
   at+=n
 gold=bytearray(raw)
 for off,width in fields:gold[off:off+width]=int.from_bytes(raw[off:off+width],'little').to_bytes(width,'big')
 assert dst.raw==gold,(kind,'big endian field oracle')
 assert f(kind,src,len(raw),0,dst,len(raw),C.byref(written))==0 and dst.raw==raw
 sentinel=b'\xa5'*len(raw);dst.raw=sentinel
 assert f(kind,src,len(raw),1,dst,len(raw)-1,C.byref(written))<0 and written.value==0 and dst.raw==sentinel
 assert f(kind,src,len(raw),1,src,len(raw),C.byref(written))<0
 if kind==6:
  corruptions=[]
  def mutate(off,data):
   bad=bytearray(raw);bad[off:off+len(data)]=data;corruptions.append(bad)
  mutate(0,struct.pack('<I',0xffffffff));mutate(0xa4,b'\x01');mutate(328+4,b'\x01');mutate(328+8,b'\xff\xff');mutate(328+0x188,b'\x7f');mutate(328+0x788,b'\x7f')
  rule=328+sizes[0];mutate(rule,b'\xff\xff');mutate(rule+4,b'\x01');mutate(rule+8,b'\x40\0');mutate(rule+12,b'\x80');mutate(9*4,struct.pack('<I',0x584))
  for bad in corruptions:
   dst.raw=sentinel;assert f(kind,C.create_string_buffer(bytes(bad)),len(raw),1,dst,len(raw),C.byref(written))<0 and written.value==0 and dst.raw==sentinel
 results.append({'kind':kind,'bytes':len(raw),'numeric_fields':len(fields)})
sys.path.insert(0,str(up/'port/tools'));import portsrc
text,_,_=portsrc.enable_attempts((up/'src/battle/btl_ai_cond.c').read_text());original=out/'btl_ai_cond.c';original.write_text(text)
sanitize='undefined,bounds' if a.i386_root else 'address,undefined';opts=['-std=gnu99','-O1','-g','-fcommon','-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-fsanitize='+sanitize,'-fno-omit-frame-pointer',*includes,'-I'+str(up/'include'),'-I'+str(up/'port/include')];link=[]
if a.i386_root:
 root=a.i386_root.resolve();lib32=root/'usr/lib32';opts+=['-m32','-B'+str(lib32)+'/', '-B'+str(root/'usr/lib/gcc/x86_64-linux-gnu/13/32')+'/', '-isystem',str(root/'usr/include'),'-isystem',str(root/'usr/include/x86_64-linux-gnu'),'-isystem','/usr/include/x86_64-linux-gnu'];link=['-m32','-B'+str(lib32)+'/', '-B'+str(root/'usr/lib/gcc/x86_64-linux-gnu/13/32')+'/', '-L'+str(lib32),'-Wl,--dynamic-linker='+str(lib32/'ld-linux.so.2'),'-Wl,-rpath,'+str(lib32)]
objs=[];log=''
for i,src in enumerate([engine/'tests/test_battle_common.c',*sources,original]):
 obj=out/(str(i)+'.o');extra=['-include',str(up/'port/include/port_compat.h')] if src==original else []
 run=subprocess.run(['gcc',*opts,*extra,'-c',str(src),'-o',str(obj)],capture_output=True,text=True);log+=run.stdout+run.stderr;(out/'compile.txt').write_text(log);run.check_returncode();objs.append(obj)
run=subprocess.run(['gcc',*link,'-fsanitize='+sanitize,*map(str,objs),'-Wl,--gc-sections','-o',str(out/'common-test')],capture_output=True,text=True);log+=run.stdout+run.stderr;(out/'compile.txt').write_text(log);run.check_returncode()
run=subprocess.run(([str(a.qemu.resolve()),'-E','LD_LIBRARY_PATH='+str(a.i386_root.resolve()/'usr/lib32')] if a.qemu else [])+[str(out/'common-test'),str(a.common.resolve())],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'test.txt').write_text(run.stdout+run.stderr);print(run.stdout+run.stderr,end='');run.check_returncode()
report={'result':'PASS','tables':results,'negative_AI_cases':11,'endian_field_oracle':True,'source_lifetime_checked':True,'original_AiThink_BindData_equivalence':bool(a.i386_root),'host_pointer_bits':32 if a.i386_root else 64,'sanitizers':sanitize,'BtlChar_AllocAll_executed':False,'BtlAiMgr_Init_executed':False,'PowerPC_execution':False}
(out/'battle-common-test.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
