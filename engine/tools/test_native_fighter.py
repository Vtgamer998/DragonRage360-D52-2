#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
from pathlib import Path
import argparse,subprocess,sys,os,json
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('model',type=Path);p.add_argument('motions',type=Path);p.add_argument('output',type=Path);p.add_argument('--i386-root',type=Path);p.add_argument('--qemu',type=Path);p.add_argument('--program',type=Path);p.add_argument('--other-model',type=Path);p.add_argument('--other-motions',type=Path);a=p.parse_args();up=a.upstream.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);e=Path(__file__).resolve().parents[1];project=e.parent
sys.path.insert(0,str(up/'port/tools'));import portsrc
s,_,_=portsrc.enable_attempts((up/'src/battle/bobj_a.c').read_text());from fixed_profile_source import transform
s,_=transform('src/battle/bobj_a.c',s);mathtext,_,_=portsrc.enable_attempts((up/'src/sys/math3d.c').read_text());mathsrc=out/'math3d.c';mathsrc.write_text(mathtext);original=out/'bobj_a.c';original.write_text(s)
mathftext,_,_=portsrc.enable_attempts((up/'src/sys/mathf.c').read_text());mathfsrc=out/'mathf.c';mathfsrc.write_text(mathftext)
original_geometry=[]
for stem in ('col_a','col_b','stg_d','btl_obj','bobj_b_b'):
 text,_,_=portsrc.enable_attempts((up/'src/battle'/(stem+'.c')).read_text());src=out/(stem+'.c');src.write_text(text);original_geometry.append(src)
for stem in ('gfxm_e_b','gfxm_b_b','gfxm_e_c'):
 text,_,_=portsrc.enable_attempts((up/'src/sys'/(stem+'.c')).read_text());text,_=transform('src/sys/'+stem+'.c',text);src=out/(stem+'.c');src.write_text(text);original_geometry.append(src)
sources=[e/'tests/test_native_fighter.c',e/'platform/platform/xb_battle_fighter.c',e/'platform/platform/xb_fighter_tables.c',e/'platform/platform/xb_fighter_registry.c',e/'platform/platform/xb_model_native.c',up/'src/sys/list.c',original,*[project/'src'/(s+'.c') for s in ('dr_body_model','dr_face_vif','dr_container','dr_anim','dr_bpe','dr_skeleton','dr_disc','dr_texfile','dr_gs_texture')],up/'src/port/vu0_a.c',up/'src/port/vu0_b.c',mathsrc,mathfsrc,up/'port/src/mathf_pc.c',*original_geometry,up/'port/src/plat_fastvec.c']
if a.program:
 import struct
 b=a.program.read_bytes();phoff=struct.unpack_from('<I',b,28)[0];phsize,phcount=struct.unpack_from('<HH',b,42)
 for i in range(phcount):
  typ,off,va,pa,fs,ms,fl,al=struct.unpack_from('<8I',b,phoff+i*phsize)
  if typ==1 and va<=0x2f2420 and 0x2f2420+852<=va+fs:
   raw=b[off+0x2f2420-va:off+0x2f2420-va+852];break
 else:raise ValueError('real reference table absent from supplied ELF')
 import hashlib
 if hashlib.sha256(b).hexdigest()!='811188ba9b416500d921cd4d9514df0cbf42f3a41a99cf5aac5a3da37171bf99':raise ValueError('unrecognized USA program')
 values=struct.unpack('<213f',raw);ref=out/'real_reference.c'
 declarations='#include "battle/bobj_a.h"\n'
 declarations+='const float real_reference[213]={'+','.join(float(v).hex()+'f' for v in values)+'};\n'
 def memory(va,n):
  for i in range(phcount):
   typ,off,base,pa,fs,ms,fl,al=struct.unpack_from('<8I',b,phoff+i*phsize)
   if typ==1 and base<=va and va+n<=base+fs:return b[off+va-base:off+va-base+n]
  raise ValueError(hex(va))
 for addr,count in [(0x2c6c20,7),(0x2c6c40,5)]:
  vals=struct.unpack('<'+str(count*2)+'h',memory(addr,count*4));declarations+='BobjTalkStep D_%08X[%d]={'%(addr,count)+','.join('{%d,%d}'%(vals[i*2],vals[i*2+1]) for i in range(count))+'};\n'
 for addr in range(0x2c6c58,0x2c6e58,64):
  raw=memory(addr,64);keys=[struct.unpack_from('<3fI',raw,i*16) for i in range(4)]
  declarations+='BobjJawKey D_%08X[4]={'%addr+','.join('{'+','.join(float(v).hex()+'f' for v in k[:3])+',%d}'%k[3] for k in keys)+'};\n'
 ref.write_text(declarations);sources.append(ref)
common=['gcc','-std=gnu99','-O1','-g','-fcommon','-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(e/'platform/platform'),'-I'+str(project/'src'),'-I'+str(up/'include'),'-I'+str(up/'port/include'),'-I'+str(up/'port/src')]
if a.program:common+=['-DHAVE_REAL_REFERENCE']
extra=[];linkextra=[]
if a.i386_root:
 root=a.i386_root.resolve();lib=root/'usr/lib32';extra=['-m32','-B'+str(lib)+'/', '-B'+str(root/'usr/lib/gcc/x86_64-linux-gnu/13/32')+'/', '-isystem',str(root/'usr/include'),'-isystem',str(root/'usr/include/x86_64-linux-gnu'),'-isystem','/usr/include/x86_64-linux-gnu']
 linkextra=['-m32','-B'+str(lib)+'/', '-B'+str(root/'usr/lib/gcc/x86_64-linux-gnu/13/32')+'/', '-L'+str(lib),'-Wl,--dynamic-linker='+str(lib/'ld-linux.so.2'),'-Wl,-rpath,'+str(lib)]
common+=extra
sanitize='undefined,bounds' if a.i386_root else 'address,undefined'
common=[('-fsanitize='+sanitize if x.startswith('-fsanitize=') else x) for x in common]
objects=[];logs=[]
for i,src in enumerate(sources):
 obj=out/(str(i)+'.o');flags=['-include',str(up/'port/include/port_compat.h')] if src in (original,mathsrc,mathfsrc,*original_geometry) else []
 if src.name in ('vu0_a.c','vu0_b.c'):flags+=['-include','math.h','-include',str(up/'port/src/vu0_names.h'),'-DREF_VU0_EXTERN_ARITH']
 r=subprocess.run(common+flags+['-c',str(src),'-o',str(obj)],capture_output=True,text=True);logs.append(r.stdout+r.stderr);(out/'compile.txt').write_text(''.join(logs));r.check_returncode();objects.append(obj)
r=subprocess.run(['gcc',*linkextra,'-fsanitize='+sanitize,*map(str,objects),'-Wl,--gc-sections','-lm','-o',str(out/'native-fighter')],capture_output=True,text=True);logs.append(r.stdout+r.stderr);(out/'compile.txt').write_text(''.join(logs));r.check_returncode()
r=subprocess.run(([str(a.qemu.resolve()),'-E','LD_LIBRARY_PATH='+str(a.i386_root.resolve()/'usr/lib32')] if a.qemu else [])+[str(out/'native-fighter'),str(a.model.resolve()),str(a.motions.resolve())]+([str(a.other_model.resolve()),str(a.other_motions.resolve())] if a.other_model and a.other_motions else []),capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'native-fighter-test.txt').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
(out/'native-fighter-test.json').write_text(json.dumps({'result':'PASS','original_PlayModel_and_Step':True,'frames':200,'owned_state_survives_source_free':True,'sanitizers':sanitize,'host_pointer_bits':32 if a.i386_root else 64,'host_only':True,'pose_matrix_and_volume_paths':bool(a.i386_root),'cross_owner_retarget':bool(a.i386_root and a.other_model),'typed_character_tables':8,'real_common_reference_supplied':bool(a.program),'common_table_retarget_executed':bool(a.i386_root and a.program),'original_inactive_object_registry_executed':bool(a.i386_root),'original_BtlObj_Setup_and_Destroy_executed':bool(a.i386_root),'original_BtlObj_CreateChara_executed':bool(a.i386_root),'two_models_in_original_pools_tested':bool(a.i386_root and a.other_model),'original_CPU_blink_lip_and_held_jaw_executed':bool(a.i386_root and a.program),'audio_clock_synchronized':False,'fighter_spawned_in_combat':False,'rendered':False},indent=2)+'\n')
