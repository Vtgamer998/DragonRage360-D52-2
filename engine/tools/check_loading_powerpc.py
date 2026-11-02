#!/usr/bin/env python3
"""Compile full adapted loading units for PowerPC; no link or runtime claim."""
import argparse,sys,subprocess,json
from pathlib import Path
from fixed_profile_source import transform
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('compiler',type=Path);p.add_argument('output',type=Path);p.add_argument('--xsdk',type=Path);a=p.parse_args()
up=a.upstream.resolve();cc=a.compiler.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);project=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(up/'port/tools'));import portsrc,eeconst
flags=['-mcpu=cell','-mcheck-fpr-alignment','-fno-pic','-std=gnu89','-O2','-fno-builtin','-fno-strict-aliasing','-ffp-contract=off','-fcommon','-DPORT_BIGENDIAN','-DDR_XBOX360','-include',str(up/'port/include/port_compat.h'),'-I'+str(up/'include'),'-I'+str(up/'port/include'),'-I'+str(project/'engine/platform/platform'),'-I'+str(project/'src')]
if a.xsdk:flags+=['-I'+str(a.xsdk.resolve()/'include')]
rows=[]
for rel in ['src/sys/common.c','src/main.c','engine/platform/platform/xb_battle_common.c','engine/platform/platform/xb_common_items.c','src/sys/file.c','src/sys/loading.c','src/battle/battle_work.c','src/battle/battle.c','src/battle/battle_load.c','src/battle/btl_obj.c','engine/platform/platform/xb_bpe.c','engine/platform/platform/xb_demo_resources.c','src/dr_texfile.c','src/sys/snd.c','engine/platform/platform/xb_sound_bank.c','engine/combat/heap.c','src/battle/bobj_a.c','engine/platform/platform/xb_motion_pack.c','engine/platform/platform/xb_resconv.c','engine/platform/platform/xb_fighter_registry.c']:
 source=project/'engine/combat/main.c' if rel=='src/main.c' else project/'engine/combat/battle.c' if rel=='src/battle/battle.c' else project/rel if rel.startswith(('engine/','src/dr_')) else up/rel
 unitflags=flags+(['-std=gnu17'] if rel.startswith(('src/dr_','engine/platform/')) else [])
 text,_,_=portsrc.enable_attempts(source.read_text());text,_=transform(rel,text)
 key=rel.replace('/','_');src=out/key;src.write_text(text);pre=out/(key+'.i');obj=out/(key+'.o')
 r=subprocess.run([str(cc),*unitflags,'-E',str(src)],capture_output=True,text=True);r.check_returncode();pre.write_text(eeconst.transform(r.stdout))
 r=subprocess.run([str(cc),*unitflags,'-fpreprocessed','-c',str(pre),'-o',str(obj)],capture_output=True,text=True);(out/(key+'.log')).write_text(r.stdout+r.stderr);r.check_returncode();rows.append(rel)
report={'result':'PASS','target':'elf32-powerpc','full_translation_units':rows,'link_verified':False,'runtime_verified':False,'native_PS2_float_pass_applied':False}
(out/'powerpc-compilation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
