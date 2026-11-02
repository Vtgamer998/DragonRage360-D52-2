#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Classify unresolved symbol relocations in real allocated ELF32-BE sections.

Usage: audit_engine_refs.py BUILD_DIR OUT
This does not remove table references. Data-only callbacks may be essential.
Objects include sections later discarded by the linker; this is a source
diagnostic, not proof that a reference executes or is unreachable at runtime.
"""
import argparse,json,struct
from pathlib import Path

def relocations(path):
    data=path.read_bytes()
    if data[:6]!=b'\x7fELF\x01\x02':raise ValueError('expected ELF32 big endian: '+str(path))
    e=struct.unpack_from('>16sHHIIIIIHHHHHH',data)
    off,stride,count,names=e[6],e[11],e[12],e[13]
    sections=[struct.unpack_from('>10I',data,off+i*stride) for i in range(count)]
    def blob(s):return data[s[4]:s[4]+s[5]]
    def string(b,o):return b[o:b.index(0,o)].decode('utf-8',errors='replace')
    secnames=blob(sections[names])
    for section in sections:
        if section[1] not in (4,9):continue
        target=sections[section[7]]
        if not target[2]&2:continue # SHF_ALLOC; omit debug relocations.
        table=sections[section[6]];strings=blob(sections[table[6]])
        symbols=blob(table);records=blob(section)
        size=section[9] or (12 if section[1]==4 else 8)
        for start in range(0,len(records),size):
            offset,info=struct.unpack_from('>II',records,start)
            sym=struct.unpack_from('>IIIBBH',symbols,(info>>8)*(table[9] or 16))
            if sym[5]!=0:continue # Only external undefined references.
            yield string(strings,sym[0]),{'object':str(path),'section':string(secnames,target[0]),
                'offset':offset,'relocation':info&255,'code':bool(target[2]&4)}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('build',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    report=json.loads((a.build/'battle-integration.json').read_text())
    args=json.loads((a.build/'link-arguments.json').read_text())
    paths=list(dict.fromkeys(Path(s) for s in args if s.endswith('.o')))
    refs={s:[] for s in report['unresolved']}
    for path in paths:
        for name,row in relocations(path):
            if name in refs:refs[name].append(row)
    groups={s:('with_code_reference' if any(r['code'] for r in rows) else
        'data_only' if rows else 'not_found_in_objects') for s,rows in refs.items()}
    result={'unresolved_count':len(refs),'object_count':len(paths),
        'counts':{s:list(groups.values()).count(s) for s in
            ['with_code_reference','data_only','not_found_in_objects']},
        'references':{s:{'classification':groups[s],'sites':rows} for s,rows in refs.items()},
        'scope':'allocated ELF32-BE sections, including sections later GC-discarded; no runtime reachability claim',
        'tables_removed':False,'battle_executed':False}
    a.output.mkdir(parents=True,exist_ok=True)
    (a.output/'references.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='references'},indent=2))
if __name__=='__main__':main()
