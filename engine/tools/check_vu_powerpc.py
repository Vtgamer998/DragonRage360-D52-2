#!/usr/bin/env python3
"""Compile reference VU source for native xsdk/Cell ABI. No runtime claim.
Usage: check_vu_powerpc.py repo compiler output.
"""
import json,pathlib,subprocess,sys
engine=pathlib.Path(__file__).resolve().parents[1];repo=pathlib.Path(sys.argv[1]).resolve()
cc=str(pathlib.Path(sys.argv[2]).resolve());out=pathlib.Path(sys.argv[3]).resolve();out.mkdir(parents=True,exist_ok=True)
flags=['-mcpu=cell','-std=gnu17','-O2','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(engine/'vu')]
for name in ['dr_vu1.c','dr_vu1_selftest.c']:
 subprocess.run([cc,*flags,'-c',str(engine/'vu'/name),'-o',str(out/pathlib.Path(name).with_suffix('.o'))],check=True)
subprocess.run([cc,'-mcpu=cell','-std=gnu17','-O2','-ffp-contract=off','-DDR_XBOX360','-I'+str(repo/'port/src/gs'),'-c',str(repo/'port/src/gs/gs_vu1.c'),'-o',str(out/'upstream-vu1.o')],check=True)
report={'compiled':'standalone original reference core, selftest and actual patched upstream gs_vu1.c','target':'Cell / xsdk ABI PowerPC big-endian','floating_point':'graphics hard-float, -ffp-contract=off; not PS2 combat soft-float ABI','execution':'not run on PowerPC/Xbox'}
(out/'vu-powerpc.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
