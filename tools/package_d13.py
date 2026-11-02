#!/usr/bin/env python3
"""Package project-owned sources and diagnostics; never include game fixtures."""
from pathlib import Path
import hashlib,json,zipfile,argparse
p=argparse.ArgumentParser();p.add_argument('output',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
expected=json.loads((root/'checkpoint-D13-final/engine-diagnostic.json').read_text())['sha256']
assert hashlib.sha256((root/'default.xex').read_bytes()).hexdigest()==expected
full=out/'DragonRage360-0.47-D13-cenarios-animados.zip'
with zipfile.ZipFile(full,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 for f in sorted(root.rglob('*')):
  rel=f.relative_to(root)
  if not f.is_file() or any(k in rel.parts for k in ['build','__pycache__','.git']) or f.suffix.lower() in ['.iso','.pyc','.o']:continue
  z.write(f,Path(root.name)/rel)
small=out/'DragonRage360-0.47-D13-XEX.zip'
with zipfile.ZipFile(small,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 for rel in ['default.xex','RELATORIO-0.47-D13.md','BUILD-RESULT.txt','checkpoint-D13-final/engine-diagnostic.json','checkpoint-D13-final/host-native-result.json','checkpoint-D13-final/xenia-291f0c9/guest-default.log']:
  z.write(root/rel,rel)
for f in [full,small]:
 with zipfile.ZipFile(f) as z:
  assert not z.testzip();name=next(n for n in z.namelist() if n.endswith('/default.xex') and 'diagnostico-' not in n or n=='default.xex')
  assert hashlib.sha256(z.read(name)).hexdigest()==expected
 print(json.dumps({'file':str(f),'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest(),'xex_sha256':expected}))
