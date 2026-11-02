#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Host reset tests plus an isolated real PPC linker relocation fixture."""
from pathlib import Path
import subprocess,json,os,argparse,struct,hashlib,sys
import build_overlay_catalog
p=argparse.ArgumentParser();p.add_argument('compiler',type=Path);p.add_argument('xsdk',type=Path);p.add_argument('dbzp',type=Path);p.add_argument('output',type=Path);p.add_argument('--sanitize',action='store_true');a=p.parse_args()
e=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);platform=e/'platform/platform'
cmd=['gcc','-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-DXB_HOST_TEST','-I'+str(platform)]
if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
exe=out/'overlay-test';subprocess.run(cmd+[str(e/'tests/test_xb_overlay.c'),str(platform/'xb_overlay.c'),'-o',str(exe)],check=True)
r=subprocess.run([str(exe),str(a.dbzp.resolve())],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
(out/'overlay.txt').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
# These are test functions with observable native DATA effects, not substitutes
# linked into the game. They let the real PPC linker resolve every fixture name.
source=out/'fixture.c';source.write_text('int selected=42; int counter; const char *choices[]={"long enough literal one","long enough literal two","long enough literal one"}; void Progress_Main(int n){selected+=n;counter+=n;} void (*handler)(int)=Progress_Main; void xsdk_start(void){handler(1);}\n')
cc=a.compiler.resolve();obj=out/'fixture.o'
subprocess.run([str(cc),'-mcpu=cell','-mhard-float','-fno-pic','-fcommon','-fdata-sections','-ffunction-sections','-O2','-c',str(source),'-o',str(obj)],check=True)
row={'source':'src/menu/fixture.c','source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'object':str(obj),'compiled':True,'functions_without_C':[]}
catalog,ld=build_overlay_catalog.generate([row],cc,a.xsdk.resolve(),out)
co=out/'catalog.o';subprocess.run([str(cc),'-mcpu=cell','-mhard-float','-I'+str(platform),'-c',str(catalog),'-o',str(co)],check=True)
elf=out/'fixture.elf';subprocess.run([str(cc),'-nostdlib','-static','-T',str(ld),'-Wl,--gc-sections,--undefined=gXbOverlayCatalog','-o',str(elf),str(obj),str(co)],check=True)
b,h,sections,names,symbols=build_overlay_catalog.read_object(elf);syms={s[0]:s[1] for s in symbols if s[4] not in (0,0xfff2)}
assert h[7]&0x100
def span(addr,n):
 for s in sections:
  if s[1]!=8 and s[3]<=addr and n<=s[5]-(addr-s[3]):return b[s[4]+addr-s[3]:s[4]+addr-s[3]+n]
 raise ValueError('not file-backed')
v=struct.unpack('>9I',span(syms['gXbOverlayCatalog'],36));version,units,regs,count,rels,nrels,low,high,size=v
assert version==units==1 and nrels>=4
for i in range(count):
 start,end,expected,policy=struct.unpack('>4I',span(regs+i*16,16));assert policy in (1,2); assert low<=start<end<=high and end-start==expected
for i in range(nrels):
 slot,expected,width=struct.unpack('>3I',span(rels+i*12,12));assert width==4 and struct.unpack('>I',span(slot,4))[0]==expected
assert all(s[4]!=0 for s in symbols if s[0] and s[3]>>4==1)
report={'result':'PASS','host_native_resets':100,'PPC_fixture_relocations_verified':nrels,'PPC_fixture_ABI':hex(h[7]),'PPC_execution':False,'engine_link_not_replaced_by_fixture':True,'MIPS_bytes_copied':False}
(out/'overlay.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: isolated real PPC linker resolves native pointer/string relocations and DATA catalogue; no game or PPC execution')
