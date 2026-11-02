#!/usr/bin/env python3
"""Freestanding 32-bit layout/function test; run directly or via qemu-i386.
No Xbox or PowerPC emulation. Usage: test_playmodel32.py repo out [qemu-i386].
"""
import json,pathlib,subprocess,sys
engine=pathlib.Path(__file__).resolve().parents[1];repo=pathlib.Path(sys.argv[1]).resolve()
work=pathlib.Path(sys.argv[2]).resolve();work.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(repo/'port/tools'))
import portsrc,eeconst
file,_,_=portsrc.prepare(repo/'src/battle/bobj_a.c')
flags=['-m32','-std=gnu99','-O2','-w','-ffreestanding','-fno-builtin','-fno-pic','-fno-pie','-fno-stack-protector','-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-DDR_XBOX360','-I'+str(engine/'tests/host32'),'-I'+str(repo/'include'),'-I'+str(repo/'port/include'),'-I'+str(engine/'resource'),'-include',str(repo/'port/include/port_compat.h')]
source=work/'bobj.i';source.write_text(eeconst.transform(subprocess.check_output(['gcc',*flags,'-E',str(file)],text=True)))
subprocess.run(['gcc',*flags,'-fpreprocessed','-c',str(source),'-o',str(work/'bobj.o')],check=True)
exe=work/'playmodel32'
subprocess.run(['gcc',*flags,'-nostdlib','-static','-no-pie','-Wl,--gc-sections',str(work/'bobj.o'),str(engine/'resource/dr_container.c'),str(engine/'resource/dr_disc.c'),str(engine/'resource/dr_anim.c'),str(engine/'resource/dr_bpe.c'),str(engine/'tests/test_playmodel32.c'),'-o',str(exe)],check=True)
runner=[str(pathlib.Path(sys.argv[3]).resolve())] if len(sys.argv)>3 else []
p=subprocess.run([*runner,str(exe)])
if p.returncode:raise SystemExit('FAIL: PlayModel32 case/exit '+str(p.returncode))
report={'PlayModel':'PASS actual patched game function: source preserved, repeated play, fresh resource data, fighter work/no-work objects, missing/corrupt/reused ID, unregistered object','ABI':'32-bit i386 pointer layout with slot/model assertions','execution':'qemu-i386 Linux user mode' if runner else 'native i386','PowerPC_Xbox':'not executed','engine_integration':'still not a linked battle executable'}
(work/'playmodel-tests.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
