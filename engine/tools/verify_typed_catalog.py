#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Verify ALL compiled typed destinations/bindings after the FINAL PPC relink."""
from pathlib import Path
import argparse,json,struct
from build_overlay_catalog import read_object
p=argparse.ArgumentParser();p.add_argument('manifest',type=Path);p.add_argument('elf',type=Path);p.add_argument('output',type=Path);a=p.parse_args();r=json.loads(a.manifest.read_text());b,h,ss,names,syms=read_object(a.elf);defined={n:(v,size,info,sec) for n,v,size,info,sec in syms if n and sec}
def at(addr,n):
 for s in ss:
  if s[1]==1 and s[3]<=addr and addr-s[3]+n<=s[5]:return b[s[4]+addr-s[3]:s[4]+addr-s[3]+n]
 raise ValueError('catalog points outside stored ELF data')
cat=at(defined['gXbTypedMainCatalog'][0],32);version,complete,regions,nr,ops,no,hash_value=struct.unpack('>6IQ',cat)
assert version==1 and complete==int(r['complete']) and nr==len(r['regions']) and no==len(r['operations']) and hash_value==int(r['operation_source_fnv64'],16)
for i,rg in enumerate(r['regions']):
 ptr,size,source,offset=struct.unpack('>4I',at(regions+i*16,16))
 assert (ptr,size,source,offset)==(defined[rg['anchor']][0]-rg['anchor_offset'],rg['size'],rg['source_file'],rg['source_offset'])
changed=0;pointer_ops=0
for i,op in enumerate(r['operations']):
 fields=struct.unpack('>7I',at(ops+i*28,28));kind=1 if op['kind']=='pointer' else 2 if op['kind'] in ('padding','zero_invariant') else 0
 native=defined[op['target']][0]+op['addend'] if op.get('target') else 0
 assert fields==(op['region'],op['offset'],op['length'],op['width'],kind,op.get('expected',0),native),i
 if kind==1:
  pointer_ops+=1;changed+=native!=op['native_value_in_reference_link']
  if op.get('target') and defined[op['target']][2]&15==2:assert not op['addend'],'MIPS interior instruction offset cannot identify native code'
assert at(defined['gPortDataStripped'][0],4)==b'\0\0\0\2'
a.output.write_text(json.dumps({'result':'PASS','PPC_catalog_shape':'32-bit pointers; 16-byte regions; 28-byte operations; header 32 bytes','regions_verified':nr,'operations_verified':no,'pointer_operations_verified':pointer_ops,'pointer_bindings_changed_since_reference_link':changed,'all_final_addresses_symbolically_relocated':True,'catalog_complete':bool(complete),'boot_marker':2,'hardware_executed':False},indent=2)+'\n')
print('PASS: final PPC catalog:',nr,'regions,',no,'ops,',pointer_ops,'checked native bindings;',changed,'addresses changed and relocated; boot remains blocked')
