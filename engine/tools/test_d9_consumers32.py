#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Original readers, freestanding Linux i386. No Xbox/PPC/gameplay test."""
from pathlib import Path
import argparse,sys,struct,subprocess,json,shutil
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('common4',type=Path);p.add_argument('output',type=Path);p.add_argument('--runner',type=Path);a=p.parse_args()
e=Path(__file__).resolve().parents[1];project=e.parent;up=a.upstream.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);native=e/'platform/platform'
headers=out/'include';headers.mkdir(exist_ok=True)
stdint=(e/'tests/host32/stdint.h').read_text().replace('#endif','#define UINTPTR_MAX __UINTPTR_MAX__\n#define UINT32_MAX __UINT32_MAX__\n#define UINT64_MAX __UINT64_MAX__\n#endif')
(headers/'stdint.h').write_text(stdint);shutil.copyfile(e/'tests/host32/string.h',headers/'string.h')
(headers/'stdlib.h').write_text('#include <stddef.h>\nvoid *malloc(size_t);void *calloc(size_t,size_t);void free(void *);\n')
entries=[b'']*213;entries[2]=b''.join(struct.pack('<I',0x10203040^i) for i in range(414))
ai=bytearray(0x148+0x788+8*8);struct.pack_into('<9I',ai,0,0x788,*([8]*8));entries[3]=bytes(ai)
for entry,count,stride in [(4,1,36),(5,57,8),(6,62,24),(7,161,16),(8,1,20)]:entries[entry]=bytes(count*stride)
offset=215*4;offsets=[offset]
for item in entries:offset+=len(item);offsets.append(offset)
common2=struct.pack('<I',213)+struct.pack('<214I',*offsets)+b''.join(entries)
common4=a.common4.read_bytes()
(out/'fixtures.h').write_text('\n'.join('static unsigned char '+name+'[]={'+','.join(map(str,b))+'};' for name,b in [('common2',common2),('common4',common4)]))
sys.path.insert(0,str(up/'port/tools'));import portsrc
original=[]
for stem in ('btl_char_status_anim','btl_char_get','view_b_b'):
 text,_,_=portsrc.enable_attempts((up/'src/battle'/(stem+'.c')).read_text());f=out/(stem+'.c');f.write_text(text);original.append(f)
flags=['-m32','-std=gnu99','-O1','-fno-builtin','-ffreestanding','-fno-pic','-fno-pie','-fno-stack-protector','-ffunction-sections','-fdata-sections','-I'+str(headers),'-I'+str(out),'-I'+str(native),'-I'+str(project/'src'),'-I'+str(up/'include'),'-I'+str(up/'port/include')]
sources=[e/'tests/d9/test_consumers32.c',native/'xb_battle_common.c',native/'xb_battle_consumers.c',*original,*[project/'src'/(x+'.c') for x in ('dr_container','dr_disc','dr_bpe')]]
objects=[];log=''
for i,s in enumerate(sources):
 obj=out/(str(i)+'.o');extra=['-include',str(up/'port/include/port_compat.h')] if s in original else []
 r=subprocess.run(['gcc',*flags,*extra,'-c',str(s),'-o',str(obj)],capture_output=True,text=True);log+=r.stdout+r.stderr;(out/'compile.txt').write_text(log);r.check_returncode();objects.append(obj)
exe=out/'consumers32';r=subprocess.run(['gcc','-m32','-nostdlib','-static','-no-pie',*map(str,objects),'-Wl,--gc-sections','-o',str(exe)],capture_output=True,text=True);log+=r.stdout+r.stderr;(out/'compile.txt').write_text(log);r.check_returncode()
try:
 r=subprocess.run(([str(a.runner.resolve())] if a.runner else [])+[str(exe)])
except OSError as error:
 if error.errno!=8:raise
 report={'result':'COMPILED_NOT_EXECUTED','reason':'host lacks i386 execution and no runner was provided','pointer_ABI_assertions_compiled':True,'PowerPC_execution':False,'runtime_consumers_verified':False}
 (out/'tests.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report));sys.exit(0)
assert r.returncode==0,('fixture check failed at C line/exit',r.returncode)
report={'result':'PASS','execution':('QEMU-i386 Linux user mode, freestanding test allocator' if a.runner else 'Linux i386, freestanding test allocator'),'original_BtlAnim_GetFlags':414,'original_voice_entries':57,'original_character_and_item_readers':[161,4],'temporary_installation_and_cleanup_twice':True,'occupied_global_refused':True,'source_overwritten_after_preparation':True,'synthetic_fixtures_only':True,'full_manager_init':False,'PowerPC_execution':False}
(out/'tests.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
