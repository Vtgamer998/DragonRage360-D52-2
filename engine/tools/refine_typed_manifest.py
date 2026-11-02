#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Refine an existing linked catalog with bounded reader-reviewed schemas.
Keeps the reference link's symbolic bindings. Does not assert a new link or boot.
The input catalog must come from build_typed_manifest.py; source hashes match.
"""
from pathlib import Path
import argparse,copy,hashlib,json,struct
from reviewed_types import describe

def sources(disc,expected):
    elf=(disc/'SLUS_216.78').read_bytes();overlay=(disc/'BIN/DBZP.BIN').read_bytes()
    if [hashlib.sha256(x).hexdigest() for x in (elf,overlay)]!=expected:raise ValueError('source program revision mismatch')
    flat=bytearray(0x1ff180);start=struct.unpack_from('<I',elf,32)[0];step,n=struct.unpack_from('<HH',elf,46)
    for i in range(n):
        _,kind,flags,a,o,s=struct.unpack_from('<6I',elf,start+i*step)
        if flags&2 and kind!=8 and 0x100000<=a and a+s<=0x2ff180:flat[a-0x100000:a-0x100000+s]=elf[o:o+s]
    return [flat,overlay]

def refine(original,raw):
    m=copy.deepcopy(original);regions=m['regions'];specs=describe(regions,raw);units={};mapping={}
    def value(rid,at,n):
        r=regions[rid];start=r['source_offset']+at;return raw[r['source_file']][start:start+n]
    for o in original['operations']:
        if o['kind']=='pointer':mapping[o['expected']]={k:o[k] for k in ('target','addend','native_value_in_reference_link')}
        for at in range(o['offset'],o['offset']+o['length'],o['width']):
            u=dict(o,offset=at,length=o['width']);units[(o['region'],at)]=u
    def pointer(v):
        if v in mapping:return mapping[v]
        if not v:return {'target':None,'addend':0,'native_value_in_reference_link':0}
        for r in regions:
            base=(0x100000 if r['source_file']==0 else 0x334c00)+r['source_offset']
            if base+min(r['labels'].values())<=v<base+r['size']:
                return {'target':r['anchor'],'addend':v-base-r['anchor_offset'],'native_value_in_reference_link':r['native_address']+v-base}
        raise ValueError(f'new pointer lacks native binding: {v:x}')
    reviewed={}
    for sp in specs:
        rid=sp['region'];lo=sp['offset'];hi=lo+sp['size']
        for key,u in list(units.items()):
            at=u['offset'];end=at+u['length']
            if key[0]==rid and at<hi and lo<end:
                if at<lo or end>hi:raise ValueError('reviewed schema splits typed field')
                del units[key]
        for off,w,k in sp['fields']:
            at=lo+off;u={'region':rid,'offset':at,'length':w,'width':w,'kind':k,'native_value_in_reference_link':0}
            if k in ('data_pointer','function_pointer'):
                v=struct.unpack('<I',value(rid,at,4))[0];u.update(kind='pointer',expected=v,**pointer(v))
            if k=='padding' and any(value(rid,at,w)):raise ValueError('nonzero struct padding')
            key=(rid,at)
            if key in reviewed and reviewed[key]!=u:raise ValueError('incompatible reviewed schemas')
            reviewed[key]=u;units[key]=u
    prefixes=[]
    for r in regions:
        n=min(r['labels'].values());base=(0x100000 if r['source_file']==0 else 0x334c00)+r['source_offset']
        if n and (n>=16 or n!=(base+n)%16):raise ValueError('unproved alignment prefix')
        if not n:continue
        for o in original['operations']:
            if o['kind']=='pointer' and base<=o['expected']<base+n:raise ValueError('live pointer into excluded prefix')
        prefixes.append({'region':r['id'],'offset':0,'length':n,'reason':'gen_data.py convert pre-label addr&15 alignment; preserved, not source data'})
        for key,u in list(units.items()):
            if key[0]==r['id'] and u['offset']<n:
                if u['offset']+u['length']>n:raise ValueError('prefix cuts typed field')
                del units[key]
        r['copy_bytes']=sum(nbytes for at,nbytes,k,_ in r['runs'] if k!='zero' and at>=n)
    suppressed=[]
    for g in m['suppressed_source_bytes']:
        if g['offset']+g['length']>min(regions[g['region']]['labels'].values()):suppressed.append(g)
    ops=[]
    for o in sorted(units.values(),key=lambda x:(x['region'],x['offset'])):
        if ops and o['kind'] in ('scalar','padding','zero_invariant') and all(o[k]==ops[-1][k] for k in ('region','kind','width')) and ops[-1]['offset']+ops[-1]['length']==o['offset']:ops[-1]['length']+=o['length']
        else:ops.append(o)
    covered=[set() for r in regions]
    for o in ops:
        r=regions[o['region']];s=o['offset'];end=s+o['length']
        if s<0 or end>r['size'] or covered[o['region']].intersection(range(s,end)):raise ValueError('operation overlaps or out of bounds')
        covered[o['region']].update(range(s,end))
    gaps=[]
    for g in original['untyped']:
        rid=g['region'];n=min(regions[rid]['labels'].values());at=g['offset'];end=at+g['length']
        while at<end:
            if at<n or at in covered[rid]:at+=1;continue
            lo=at;at+=1
            while at<end and at not in covered[rid]:at+=1
            if not any(value(rid,lo,at-lo)):
                z={'region':rid,'offset':lo,'length':at-lo,'width':1,'kind':'zero_invariant','native_value_in_reference_link':0};ops.append(z);covered[rid].update(range(lo,at));continue
            x=dict(g,offset=lo,length=at-lo,symbol_offset=g['symbol_offset']+lo-g['offset'],source_address=g['source_address']+lo-g['offset']);gaps.append(x)
    ops.sort(key=lambda o:(o['region'],o['offset']))
    for r in regions:r['typed_bytes']=len(covered[r['id']])
    h=0xcbf29ce484222325
    for o in ops:
        for b in value(o['region'],o['offset'],o['length']):h=((h^b)*0x100000001b3)&0xffffffffffffffff
    m.update(operations=ops,untyped=gaps,suppressed_source_bytes=suppressed,alignment_prefixes=prefixes,reviewed_schemas=specs,complete=not gaps and not suppressed and not m['unresolved_pointer_bindings'],operation_source_fnv64=f'{h:016x}',safe_zero_invariant_bytes=sum(o['length'] for o in ops if o['kind']=='zero_invariant'),battle_boot_ready=False)
    m['layout_evidence']+='; bounded reader-reviewed schemas in engine/tools/reviewed_types.py'
    m['reference_link_reused']=True
    return m

def emit(m,out):
    out.mkdir(parents=True,exist_ok=True);(out/'typed-manifest.json').write_text(json.dumps(m,indent=2)+'\n')
    names={r['anchor'] for r in m['regions']}|{o['target'] for o in m['operations'] if o.get('target')}
    c=['/* Generated metadata; no game resource bytes. */','#include "xb_typed_data.h"']+['extern unsigned char '+n+'[];' for n in sorted(names)]+['static const XbTypedRegion regions[]={']
    for r in m['regions']:c.append('{'+r['anchor']+'-'+str(r['anchor_offset'])+','+','.join(str(r[k]) for k in ['size','source_file','source_offset'])+'},')
    c+=['};','static const XbTypedOp ops[]={']
    for o in m['operations']:
        native='((uintptr_t)'+o['target']+'+(intptr_t)('+str(o['addend'])+'))' if o.get('target') else '0';kind=1 if o['kind']=='pointer' else 2 if o['kind'] in ('padding','zero_invariant') else 0
        c.append('{'+','.join(str(v) for v in [o['region'],o['offset'],o['length'],o['width'],kind,o.get('expected',0)])+','+native+'},')
    c+=['};',f'const XbTypedCatalog gXbTypedMainCatalog={{1,{int(m["complete"])},regions,{len(m["regions"])},ops,{len(m["operations"])},0x{m["operation_source_fnv64"]}ull}};']
    (out/'typed_catalog.c').write_text('\n'.join(c)+'\n')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('manifest',type=Path);p.add_argument('disc',type=Path);p.add_argument('output',type=Path);a=p.parse_args();m=json.loads(a.manifest.read_text());result=refine(m,sources(a.disc,m['source_program_sha256']));emit(result,a.output)
    print(json.dumps({'gaps':len(result['untyped']),'untyped_bytes':sum(g['length'] for g in result['untyped']),'suppressed':len(result['suppressed_source_bytes']),'typed_bytes':sum(r['typed_bytes'] for r in result['regions']),'complete':result['complete']}))
