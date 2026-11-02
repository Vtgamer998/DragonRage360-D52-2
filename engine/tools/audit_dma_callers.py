#!/usr/bin/env python3
"""Heuristic audit of upstream callers of the native DMA producers.
Usage: audit_dma_callers.py upstream [--json out.json]
For each Dma_BeginDirect..Dma_EndDirect window it flags stores that are not u64
(u32/f32 casts, GsQword .w[] access). For each Dma_Alloc it flags u64/GsQword
.d[] stores. A flag means the region is NOT safely convertible with the default
kind and needs review; no flag is NOT a proof (regex on source text, no data flow)."""
import re,sys,json,argparse
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('upstream',type=Path);ap.add_argument('--json',type=Path)
a=ap.parse_args();src=a.upstream/'src'
narrow=re.compile(r'\((?:u32|s32|f32|u16|s16|u8|s8)\s*\*\)|\.w\[|->w\[|\bu32\s*\*\s*\w+\s*=\s*\(')
wide=re.compile(r'\((?:u64|s64)\s*\*\)|\.d\[|->d\[|\bGsQword\b')
rows=[]
for f in sorted(src.rglob('*.c')):
    if f.name=='dma.c':continue
    t=f.read_text(errors='replace');lines=t.splitlines()
    for m in re.finditer(r'Dma_BeginDirect\s*\(',t):
        end=t.find('Dma_EndDirect',m.end());
        if end<0:rows.append((str(f.relative_to(src)),t.count('\n',0,m.start())+1,'Direct','NO_END',[]));continue
        w=t[m.start():end];hits=sorted(set(narrow.findall(w)))
        rows.append((str(f.relative_to(src)),t.count('\n',0,m.start())+1,'Direct','REVIEW' if hits else 'u64-only?',hits))
    for m in re.finditer(r'Dma_Alloc\s*\(',t):
        w=t[m.start():m.start()+1500];nxt=w.find('Dma_',10);w=w[:nxt] if nxt>0 else w
        hits=sorted(set(wide.findall(w)))
        rows.append((str(f.relative_to(src)),t.count('\n',0,m.start())+1,'Alloc','REVIEW' if hits else 'u32-only?',hits))
    for m in re.finditer(r'Dma_AddData\s*\(',t):
        rows.append((str(f.relative_to(src)),t.count('\n',0,m.start())+1,'AddData','CHECK_SOURCE_TYPE',[]))
    for m in re.finditer(r'Dma_AddRef\s*\(',t):
        rows.append((str(f.relative_to(src)),t.count('\n',0,m.start())+1,'AddRef','CHECK_SOURCE_ENDIAN',[]))
from collections import Counter
c=Counter((r[2],r[3]) for r in rows)
for k,v in sorted(c.items()):print(f'{k[0]:8} {k[1]:22} {v}')
print('total call sites:',len(rows))
for r in rows:
    if r[3]=='REVIEW' or r[3]=='NO_END':print(f'  {r[0]}:{r[1]} {r[2]} {r[3]} {r[4]}')
if a.json:a.json.write_text(json.dumps([dict(file=r[0],line=r[1],kind=r[2],status=r[3],hits=r[4]) for r in rows],indent=1))
