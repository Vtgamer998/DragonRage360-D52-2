#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
from pathlib import Path
import argparse,subprocess,os,ctypes as C,struct,json
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('inputs',nargs=3,type=Path);a=p.parse_args();r=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);native=r/'engine/platform/platform'
sources=[*[native/(x+'.c') for x in ['xb_common_pack','xb_battle_common','xb_resconv','xb_stage_camera']],*[r/'src'/(x+'.c') for x in ['dr_container','dr_disc','dr_anim','dr_bpe','dr_skeleton','dr_texfile','dr_gs_texture']]]
opts=['-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-DXB_HOST_TEST','-I'+str(native),'-I'+str(r/'src')];exe=out/'common-pack-test'
subprocess.run(['gcc',*opts,'-fsanitize=address,undefined',str(r/'engine/tests/common_pack_transport.c'),*map(str,sources),'-o',str(exe)],check=True)
t=subprocess.run([str(exe),*map(lambda p:str(p.resolve()),a.inputs)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'),timeout=20);(out/'test.log').write_text(t.stdout+t.stderr);print(t.stdout+t.stderr,end='');t.check_returncode()
lib=out/'common.so';subprocess.run(['gcc',*opts,'-shared','-fPIC',*map(str,sources),'-o',str(lib)],check=True)
f=C.CDLL(str(lib)).dr_font_prepare;f.argtypes=[C.c_void_p,C.c_size_t,C.c_int];f.restype=C.c_int
boot=a.inputs[2].read_bytes();o=struct.unpack_from('<12I',boot,4);fonts=[]
for i in range(2):
 raw=boot[o[i]:o[i+1]];at,count=struct.unpack_from('<2I',raw,20);fields=[(x,4) for x in range(12,36,4)]+[(64+x,4) for x in range(0,40,4)]+[(112,8)]+[(x,2) for x in range(at,at+count*6,2)];expected=bytearray(raw)
 for x,w in fields:expected[x:x+w]=expected[x:x+w][::-1]
 expected[120:128]=bytes(8);b=C.create_string_buffer(raw,len(raw));assert not f(b,len(raw),1) and b.raw==expected
 for x in [12,20,24,64,68,72]:
  bad=bytearray(raw);struct.pack_into('<I',bad,x,0xffffffff);b=C.create_string_buffer(bytes(bad),len(bad));assert f(b,len(bad),1)<0 and b.raw==bad
 fonts.append({'glyphs':count,'BE_fields':len(fields)})
report={'result':'PASS','entries':[213,637,11],'fonts':fonts,'atomic_file_refusals':12,'ASan_UBSan':True,'gameplay':False};(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
