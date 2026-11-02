#!/usr/bin/env python3
"""Package source-only D47; keep D46 binary evidence explicitly historical."""
from pathlib import Path
import json,zipfile
from package_d46 import build,ROOT
out=ROOT.parent.parent/'delivery/DragonRage360-adaptacao-upstream-arquivo.zip'
assert json.loads((ROOT/'validation-d47/final/summary.json').read_text())['result']=='PASS'
result=build(out,False)
with zipfile.ZipFile(out) as z:
 assert 'DragonRage360/README-D47.md' in z.namelist()
 assert 'DragonRage360/validation-d47/final/negative-control.log' in z.namelist()
 assert not any(n.endswith('.xex') for n in z.namelist())
print(json.dumps(result,indent=2))
