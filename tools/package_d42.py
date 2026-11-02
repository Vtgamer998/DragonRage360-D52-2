#!/usr/bin/env python3
"""Package D42 source, bounded experimental executable and selected evidence; no game assets."""
from pathlib import Path
import argparse,hashlib,json,zipfile
p=argparse.ArgumentParser();p.add_argument('output',type=Path);a=p.parse_args();out=a.output.resolve();out.parent.mkdir(parents=True,exist_ok=True);r=Path(__file__).resolve().parents[1]
for variant in ['console','xenia']:
 report=json.loads((r/f'validation-d42/{variant}/static.json').read_text());assert report['source_delivery']=='D42'
assert (r/'default.xex').read_bytes()==(r/'validation-d42/console/default.xex').read_bytes()
(r/'SHA256SUMS-D42.txt').write_text('\n'.join(hashlib.sha256((r/n).read_bytes()).hexdigest()+'  '+n for n in ['default.xex','xenia/default.xex'])+'\n')
files=['default.xex','startup_test.txt','README-D42.md','RESULTADO-D42.json','SHA256SUMS-D42.txt','LICENSE','LICENSES.md','Makefile','isos/COLOQUE-SUA-ISO-AQUI.txt','xenia/default.xex','xenia/static.json','xenia/engine-diagnostic.json','xenia/startup_test.txt']
for directory in ['src','engine','tests','tools','manifesto-tipado','licenses','evidencias-d42']:
 for f in sorted((r/directory).rglob('*')):
  if f.is_file() and not any(n in f.parts for n in ['__pycache__','.git']) and f.suffix not in ['.o','.so','.elf','.pyc','.iso','.raw'] and f.name not in ['build_d40.py','package_d40.py','build_d41.py','package_d41.py']:files.append(str(f.relative_to(r)))
for name in ['battle-integration.json','link-arguments.json']:files.append('validation-d42/link/'+name)
with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED,6) as z:
 for name in dict.fromkeys(files):z.write(r/name,'DragonRage360-D42/'+name)
with zipfile.ZipFile(out) as z:
 assert z.testzip() is None
 assert not any(n.lower().endswith(('.iso','.raw','.elf','.o','.so','.pyc')) or 'private-vu.bin' in n for n in z.namelist())
 assert z.read('DragonRage360-D42/default.xex')==(r/'validation-d42/console/default.xex').read_bytes()
print(out,out.stat().st_size,hashlib.sha256(out.read_bytes()).hexdigest())
