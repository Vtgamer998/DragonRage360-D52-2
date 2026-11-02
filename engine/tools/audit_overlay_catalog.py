#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Inventory native menu sections/relocations and check reset catalogue coverage.
Does not execute PowerPC or turn a failed engine link into an executable.
"""
import json,struct,re,argparse,hashlib
from pathlib import Path
from build_overlay_catalog import read_object
p=argparse.ArgumentParser();p.add_argument('build',type=Path);a=p.parse_args()
catalog=json.loads((a.build/'overlay/catalog.json').read_text());full=[];mutable=[]
for unit in catalog['objects']:
    report=json.loads((a.build/'battle-integration.json').read_text())
    row=next(x for x in report['files'] if x['source']==unit['source'])
    path=Path(row['object']);data,h,ss,names,syms=read_object(path)
    assert hashlib.sha256(data).hexdigest()==unit['object_sha256']
    sections=[];relocations=[]
    for i,s in enumerate(ss):
        if s[2]&2 and s[5]:
            sections.append({'section':names[i],'size':s[5],'flags':s[2],'type':s[1]})
            if s[2]&3==3:mutable.append((unit['source'],names[i],s[5]))
        if s[1] in (4,9) and ss[s[7]][2]&2:
            assert s[1]==4 and s[9]==12
            for at in range(s[4],s[4]+s[5],12):
                offset,info,add=struct.unpack_from('>IIi',data,at);sym=syms[info>>8]
                relocations.append({'section':names[s[7]],'offset':offset,'PPC_type':info&255,'symbol':sym[0],'symbol_section':names[sym[4]] if 0<sym[4]<len(ss) else sym[4],'symbol_value':sym[1],'addend':add})
    full.append({'source':unit['source'],'sections':sections,'native_relocations':relocations})
declared=[(r['object'],r['section'],r['bytes']) for r in catalog['regions'] if r['section']!='COMMON']
assert sorted(mutable)==sorted(declared) and len(declared)==len(set(declared))
# The failed strict link's map still supplies layout evidence. No incomplete
# ELF or undefined-symbol suppression is needed for these boundary checks.
text=(a.build/'battle-link.map').read_text()
addresses={m.group(2):int(m.group(1),16) for m in re.finditer(r'^\s*0x([0-9a-fA-F]+)\s+(__xbo_(?:start|end|size)_\d+|__xbo_data_(?:begin|end))\s*=',text,re.M)}
low,high=addresses['__xbo_data_begin'],addresses['__xbo_data_end'];total=0
for i,r in enumerate(catalog['regions']):
    start,end=addresses[f'__xbo_start_{i}'],addresses[f'__xbo_end_{i}']
    expected=r['bytes'] if r['bytes'] is not None else addresses[f'__xbo_size_{i}']
    assert low<=start<end<=high and end-start==expected
    total+=expected
reset=sum(r['policy']==1 for r in catalog['regions']);preserved=sum(r['policy']==2 for r in catalog['regions'])
result={'result':'PASS','native_units':len(full),'native_reset_regions':reset,'persistent_port_regions':preserved,'total_mutable_bytes':total,'mutable_pointer_relocations_validated_by_runtime':len(catalog['relocations']),'all_allocated_relocations_in_inventory':sum(len(u['native_relocations']) for u in full),'writable_coverage_complete':True,'strict_link_succeeded':report['link']=='PASS','hardware_execution':False,'scope':'complete native C menu section/relocation inventory; immutable code/rodata remain under native linker; PS2 main-program resource/table schema is separate','units':full}
(a.build/'overlay/full-inventory.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='units'},indent=2))
