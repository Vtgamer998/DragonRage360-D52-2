#!/usr/bin/env python3
"""Compile changed actual game files and resource modules with the xsdk ABI.
Prerequisite: apply patches/battle-resources-powerpc.patch to the pinned upstream.
This does not link the game or execute it on PowerPC.
"""
import json,pathlib,subprocess,sys
repo=pathlib.Path(sys.argv[1]).resolve();cc=str(pathlib.Path(sys.argv[2]).resolve());out=pathlib.Path(sys.argv[3]).resolve();out.mkdir(parents=True,exist_ok=True)
engine=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(repo/'port/tools'))
import portsrc,eeconst
rows=[]
for rel in ['src/sys/mem_read.c','src/sys/bpe.c','src/battle/bobj_a.c']:
    file=repo/rel
    if 'DR_XBOX360' not in file.read_text():raise SystemExit('Apply battle-resources-powerpc.patch first')
    prepared,_,_=portsrc.prepare(file)
    flags=['-std=gnu89','-O2','-w','-fno-strict-aliasing','-ffp-contract=off','-fcommon','-fno-builtin','-msoft-float','-DDR_XBOX360','-include','port_libm.h','-Iinclude','-Iport/include','-include','port_compat.h','-I'+str(engine/'resource'),'-I'+str(file.parent)]
    pre=out/(file.stem+'.i');obj=out/(file.stem+'.o')
    p=subprocess.run([cc,*flags,'-E',str(prepared)],cwd=repo,capture_output=True,text=True)
    if p.returncode:raise SystemExit(p.stderr)
    pre.write_text(eeconst.transform(p.stdout))
    subprocess.run([cc,*flags,'-fpreprocessed','-c',str(pre),'-o',str(obj)],cwd=repo,check=True)
    pre.unlink();rows.append(rel)
for file in sorted((engine/'resource').glob('*.c')):
    subprocess.run([cc,'-std=gnu17','-O2','-Wall','-Wextra','-Werror','-msoft-float','-c',str(file),'-o',str(out/file.with_suffix('.o').name)],check=True)
    rows.append(str(file.relative_to(engine)))
subprocess.run([cc,'-std=gnu17','-include',str(repo/'port/include/port_compat.h'),'-I'+str(repo/'include'),'-c',str(engine/'tests/resource_layout.c'),'-o',str(out/'resource_layout.o')],check=True)
rows.append('tests/resource_layout.c (15 format assertions)')
report={'target':'powerpc-xsdk-elf, game software-float ABI','compiled':rows,'runtime':'not executed','engine_link':'not attempted; platform/render/resource blockers remain'}
(out/'powerpc-resources.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
