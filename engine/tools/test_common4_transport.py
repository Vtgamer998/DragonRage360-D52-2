#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Real file4 transport, independent endian oracle and original table readers."""
from pathlib import Path
import argparse,subprocess,ctypes as C,struct,json,os,sys
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('input',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[2];up=a.upstream.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);native=root/'engine/platform/platform'
sources=[native/'xb_battle_common.c',*[root/'src'/(s+'.c') for s in ['dr_container','dr_disc','dr_anim','dr_bpe','dr_skeleton']]]
flags=['-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-I'+str(native),'-I'+str(root/'src')]
lib=out/'common.so';subprocess.run(['gcc',*flags,'-shared','-fPIC',*map(str,sources),'-o',str(lib)],check=True)
f=C.CDLL(str(lib)).XbBattleCommon_PrepareItems;f.argtypes=[C.c_void_p,C.c_size_t,C.c_int];f.restype=C.c_int
raw=a.input.read_bytes();n=len(raw);count,ca,it,end=struct.unpack_from('<4I',raw);assert count==2 and end==n
ca&=~3;it&=~3;nc=(it-ca)//60;ni=(n-it)//40
fields=[(i,4) for i in range(0,16,4)]
for at in range(ca,ca+nc*60,60):fields += [(at+j,4) for j in [0,4,*range(16,44,4),48,52]]+[(at+j,2) for j in range(8,16,2)]
for at in range(it,it+ni*40,40):fields += [(at+j,2) for j in range(12,20,2)]+[(at+j,4) for j in range(20,40,4)]
gold=bytearray(raw)
for at,w in fields:gold[at:at+w]=gold[at:at+w][::-1]
for be,expected in [(0,raw),(1,gold)]:
 b=C.create_string_buffer(raw,n);assert not f(b,n,be) and b.raw==expected
for length in [0,63,n-1,n+1]:
 b=C.create_string_buffer(raw+b'\xa5');before=b.raw;assert f(b,length,1)<0 and b.raw==before
sys.path.insert(0,str(up/'port/tools'));import portsrc
text,_,_=portsrc.enable_attempts((up/'src/battle/view_b_b.c').read_text());original=out/'view_b_b.c';original.write_text(text)
objs=[];log='';opts=flags+['-Wno-unused-parameter','-fcommon','-fno-builtin','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-DXB_HOST_TEST','-I'+str(up/'include'),'-I'+str(up/'port/include')]
for i,src in enumerate([root/'engine/tests/common4_transport.c',native/'xb_resconv.c',native/'xb_common_items.c',*sources,original]):
 obj=out/(str(i)+'.o');extra=['-include',str(up/'port/include/port_compat.h')] if src==original else []
 r=subprocess.run(['gcc',*opts,*extra,'-c',str(src),'-o',str(obj)],capture_output=True,text=True);log+=r.stdout+r.stderr;(out/'compile.log').write_text(log);r.check_returncode();objs.append(obj)
exe=out/'common4-transport';subprocess.run(['gcc','-fsanitize=address,undefined',*map(str,objs),'-Wl,--gc-sections','-o',str(exe)],check=True)
r=subprocess.run([str(exe),str(a.input.resolve())],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'),timeout=20);(out/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
report={'result':'PASS','real_file_bytes':n,'characters':nc,'items':ni,'independent_BE_field_oracle':len(fields),'original_readers_host':True,'PowerPC_execution':False,'battle':False};(out/'common4-transport.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
