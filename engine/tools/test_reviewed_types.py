#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Reader-schema boundary tests with user-provided USA program bytes."""
from pathlib import Path
import argparse,copy,json,struct
from reviewed_types import describe
from refine_typed_manifest import sources,refine
p=argparse.ArgumentParser();p.add_argument('baseline',type=Path);p.add_argument('disc',type=Path);a=p.parse_args();m=json.loads(a.baseline.read_text());raw=sources(a.disc,m['source_program_sha256']);specs=describe(m['regions'],raw)
assert len(specs)==82
out=refine(m,raw);assert len(out['untyped'])==3 and sum(g['length'] for g in out['untyped'])==46 and not out['suppressed_source_bytes'] and not out['complete'] and not out['battle_boot_ready']
for n in ['D_002EDC70','D_002EDCE0','D_002EDD50']:
 assert any(n in r['labels'] for r in m['regions'])
# Each of these corruptions must be refused before any schema is emitted.
menus=[s for s in specs if s['evidence'].startswith('battle/hud_0.h BtlMenu;')];icons=[s for s in specs if 'FontIconName;' in s['evidence']];definitions=[s for s in specs if 'FontIconDef;' in s['evidence']]
cases=[(menus[0],4,0xffffffff),(menus[0],0,0xfffffffc),(menus[0],8,0x7fc00000),(icons[0],8,5)]
for spec,off,value in cases:
 bad=[bytearray(x) for x in raw];r=m['regions'][spec['region']];at=r['source_offset']+spec['offset']+off;struct.pack_into('<I',bad[r['source_file']],at,value)
 try:describe(m['regions'],bad)
 except ValueError:pass
 else:raise AssertionError('schema accepted malformed graph/count/NaN')
# Non-first-label zero suppressions must remain a real blocker.
bad=copy.deepcopy(m);r=bad['regions'][0];bad['suppressed_source_bytes'].append({'region':0,'offset':1000,'length':4,'reason':'test'})
assert len(refine(bad,raw)['suppressed_source_bytes'])==1
# Prefix proof may not swallow a native pointer to the excluded bytes.
bad=copy.deepcopy(m);r=next(r for r in bad['regions'] if min(r['labels'].values())>0);pointer=next(o for o in bad['operations'] if o['kind']=='pointer');pointer['expected']=0x100000+r['source_offset']
try:refine(bad,raw)
except ValueError:pass
else:raise AssertionError('live prefix pointer hidden')
print('PASS: 82 bounded reviewed schemas; 3/46 pool ranges retained until separate native reference proof; bad menu graph/count/NaN/icon lengths refused; real suppressions and live prefix pointers remain blockers; boot stays disabled')
