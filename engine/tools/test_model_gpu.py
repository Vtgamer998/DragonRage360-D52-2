#!/usr/bin/env python3
"""Build transient microprogram fixtures from upstream VSM, never ship game binaries."""
import argparse,importlib.util,subprocess,struct,json,hashlib,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--vuasm',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();r=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('vuasm',a.vuasm.resolve());asm=importlib.util.module_from_spec(spec);spec.loader.exec_module(asm)
fixtures=[];hashes=[]
for program,size,expected in [('0',127,0x931162449bb92b6f),('1',124,0x1a6a4fafbcad4df0),('2a',99,0xc81f82b235544790),('2b',101,0x1a9bf60fb69a640f)]:
 b=asm.assemble(str(r/f'vendor/tenkaichi-original/src/vu1/prog{program}.vsm'));m=bytearray(16384);i=end=0
 while i<len(b):
  c=struct.unpack_from('<I',b,i)[0];i+=4
  if not c:continue
  assert c>>24&127==0x4a;n=c>>16&255;n=n or 256;at=(c&65535)*8
  assert at+n*8<=len(m) and i+n*8<=len(b)
  m[at:at+n*8]=b[i:i+n*8];i+=n*8;end=max(end,at+n*8)
 h=14695981039346656037
 for x in m[:end]:h=((h^x)*1099511628211)&((1<<64)-1)
 assert end==size*8 and h==expected
 f=out/f'private-vu-{size}.bin';f.write_bytes(m[:end]);fixtures.append(f);hashes.append(hex(h))
exe=out/'check'
subprocess.run(['gcc','-std=gnu17','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined','-fno-pie','-no-pie',str(r/'engine/tests/model_gpu_bounds.c'),'-lm','-o',str(exe)],check=True)
run=subprocess.run([str(exe),*map(str,fixtures[:2])],text=True,capture_output=True,check=True);(out/'test.log').write_text(run.stdout+run.stderr);print(run.stdout,end='')
counts=re.search(r'reference VM instructions=(\d+), shader-path VM instructions=(\d+)',run.stdout);assert counts
report={'same_synthetic_strip_VM_instructions':{'reference':int(counts[1]),'shader_path':int(counts[2]),'GPU_FPS_benchmark':False},'result':'PASS','program_hashes':hashes,'fixture':'original VSM assembled with official scripts/vuasm.py; transient binaries excluded from delivery','scope':'classification, endian-safe bounded layout, callback and refusal paths; no GPU pixels or console FPS measurement'}
(out/'model-gpu.json').write_text(json.dumps(report,indent=2)+'\n')

shadow=out/'check-shadow'
subprocess.run(['gcc','-std=gnu17','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined','-fno-pie','-no-pie',str(r/'engine/tests/shadow_gpu_bounds.c'),'-lm','-o',str(shadow)],check=True)
run=subprocess.run([str(shadow),*map(str,fixtures[2:])],text=True,capture_output=True,check=True)
(out/'shadow.log').write_text(run.stdout+run.stderr);print(run.stdout,end='')
report['shadow_tests']='PASS';report['shadow_instruction_counts']=[{'reference':int(x),'shader':int(y)} for x,y in re.findall(r'reference=(\d+) shader=(\d+)',run.stdout)]
(out/'model-gpu.json').write_text(json.dumps(report,indent=2)+'\n')
