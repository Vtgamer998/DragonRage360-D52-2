#!/usr/bin/env python3
"""Rebuild both bounded D42 diagnostics. This does not enable full-game boot."""
import argparse,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--xsdk',required=True,type=Path);p.add_argument('--upstream',required=True,type=Path);p.add_argument('--toolchain-prefix',required=True);p.add_argument('--plugin-include',required=True,type=Path);p.add_argument('--output',type=Path,default=Path('build/d42'));p.add_argument('--jobs',type=int,default=4)
a=p.parse_args();root=Path(__file__).resolve().parents[1];sdk=a.xsdk.resolve();up=a.upstream.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
assert 1<=a.jobs<=32
pin='3ca419c7966b11438e2dc3c9a7e1914f102ed545'
assert subprocess.check_output(['git','-C',str(up),'rev-parse','HEAD'],text=True).strip()==pin,'use the pinned combat revision'
assert 'XSDK_DISPLAY_KERNEL_KEEP_LUT' in (sdk/'include/xsdk/display.h').read_text(),'apply engine/platform/xsdk-keep-kernel-lut.patch first'
prefix=str(Path(a.toolchain_prefix).resolve());cc=prefix+'gcc';base=re.search(r'^OBJECTS := (.+)$',(root/'Makefile').read_text(),re.M);assert base
commands=[['make','-C',str(sdk),f'-j{a.jobs}','TOOLCHAIN='+prefix,'build/libxsdk.a'],['make','-C',str(root),f'-j{a.jobs}','XSDK='+str(sdk),'UPSTREAM='+str(up),'TOOLCHAIN='+prefix,*base[1].split()],['python3',str(root/'engine/tools/link_battle.py'),str(up),cc,str(sdk),str(out/'link'),'--platform','--fixed-battle','--typed-catalog',str(root/'manifesto-tipado/typed_catalog.c'),'--float-mode','native-ps2','--plugin-include',str(a.plugin_include.resolve()),'--jobs',str(a.jobs)]]
for variant in ['console','xenia']:
 commands.append(['python3',str(root/'engine/tools/build_engine_diagnostic.py'),str(out/'link'),str(sdk),str(out/variant),*(['--xenia-display'] if variant=='xenia' else [])])
 commands.append(['python3',str(root/'tests/check_xex.py'),str(out/variant),str(out/variant/'static.json')])
(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
for i,cmd in enumerate(commands):
 print(f'[{i+1}/{len(commands)}] {cmd[0]} {cmd[1]}',flush=True)
 with (out/f'step-{i+1}.log').open('w') as log:subprocess.run(cmd,cwd=root,stdout=log,stderr=subprocess.STDOUT,check=True)
print('Both diagnostic XEX builds passed static verification. NOT_READY=2; runtime proof is separate.')
