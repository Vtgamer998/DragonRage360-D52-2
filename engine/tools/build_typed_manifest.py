#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generate an explicitly typed partial catalog for the linked native engine.
Input layout evidence is the actual PPC DWARF, not host sizeof guesses.
No commercial source bytes are written to the output manifest.
"""
from pathlib import Path
import argparse,concurrent.futures,hashlib,json,re,struct,sys,subprocess
import typed_dwarf
from reviewed_types import describe as reviewed_types
from reviewed_unused import audit as unused_audit,with_link_objects
from build_overlay_catalog import read_object
from native_data_sections import RULES

def skeleton(path):
    off=0;labels={};runs=[]
    for line in path.read_text().splitlines():
        m=re.fullmatch(r'(\w+):',line)
        if m:labels[m[1]]=off
        m=re.match(r'\s*\.(space|zero) (\d+)',line)
        if m:runs.append((off,int(m[2]),m[1],None));off+=int(m[2])
        m=re.match(r'\s*\.long (.+)',line)
        if m:runs.append((off,4,'pointer',m[1]));off+=4
    return off,labels,runs

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('upstream',type=Path);p.add_argument('build',type=Path);p.add_argument('disc',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    root=a.upstream.resolve();build=a.build.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    report=json.loads((build/'battle-integration.json').read_text());assert report['link']=='PASS'
    cc=Path(json.loads((build/'link-arguments.json').read_text())[0]);readelf=cc.with_name(cc.name[:-3]+'readelf')
    b,h,ss,names,syms=read_object(build/'battle-link.elf');native={name:(v,size,info) for name,v,size,info,sec in syms if name and sec}
    known={}
    for f in (root/'config/symbols').glob('*.txt'):
        for m in re.finditer(r'^(\w+)\s*=\s*0x([0-9a-fA-F]+)',f.read_text(),re.M):known[m[1]]=int(m[2],16)
    def original_symbol(name):
        if name in known:return known[name]
        m=re.fullmatch(r'D_([0-9A-F]{8})',name);return int(m[1],16) if m else None
    # All symbolic native destinations, including BSS, form the pointer map.
    reverse={}
    for name in native:
        old=original_symbol(name)
        if old is not None:reverse.setdefault(old,[]).append(name)
    elf=(a.disc/'SLUS_216.78').read_bytes();shoff=struct.unpack_from('<I',elf,32)[0];shstep,n=struct.unpack_from('<HH',elf,46)
    flat=bytearray(0x1ff180);cover=[]
    for i in range(n):
        _,kind,flags,address,off,size=struct.unpack_from('<6I',elf,shoff+i*shstep)
        if flags&2 and kind!=8 and 0x100000<=address and address+size<=0x2ff180:
            flat[address-0x100000:address-0x100000+size]=elf[off:off+size];cover.append((address-0x100000,address-0x100000+size))
    raw=[bytes(flat),(a.disc/'BIN/DBZP.BIN').read_bytes()]
    if hashlib.sha256(elf).hexdigest()!='811188ba9b416500d921cd4d9514df0cbf42f3a41a99cf5aac5a3da37171bf99' or hashlib.sha256(raw[1]).hexdigest()!='30f61f9c78c3859e5dd4fcfd8df6753b0f2d2c64beb924b92da5b2eaaa5e09cc':
        raise ValueError('unsupported source program revision; review the USA schema first')
    index={}
    for line in (root/'port/data/index.txt').read_text().splitlines():
        fields=line.split()
        if len(fields)==3 and not line.startswith('#'):index[fields[0]+'.s']=(int(fields[1]),int(fields[2],16))
    regions=[];wanted={}
    for path in sorted((root/'port/data').glob('*.s')):
        if path.name not in index:continue
        rel='port/data/'+path.name;size,labels,runs=skeleton(path);which,source=index[path.name];limit=size
        if rel in RULES:limit=RULES[rel]['cut']
        if not limit:continue
        present={name:off for name,off in labels.items() if off<limit and name in native}
        if not present:continue
        offsets={native[name][0]-off for name,off in present.items()}
        if len(offsets)!=1:raise ValueError('native data layout changed: '+rel)
        base=offsets.pop();anchor=min(present,key=present.get)
        if source+limit>len(raw[which]):raise ValueError('source extent')
        if which==0 and not any(lo<=source and source+limit<=hi for lo,hi in cover):raise ValueError('ELF data not fully covered')
        region={'id':len(regions),'file':rel,'source_file':which,'source_offset':source,'size':limit,'native_address':base,'anchor':anchor,'anchor_offset':present[anchor],'labels':present,'runs':[r for r in runs if r[0]<limit]}
        regions.append(region)
        cuts=sorted(set(labels.values())|{limit})
        for name,off in present.items():wanted[name]=(region['id'],off,next((v-off for v in cuts if v>off),limit-off))
    candidates={};layouts_seen=0
    jobs=[row for row in report['files'] if row['source'].startswith(('src/','engine/combat/')) and row['source'].endswith('.c')]
    def unit(row):
        variables,layout=typed_dwarf.read(Path(row['object']),readelf);matches=[]
        for name,t in variables:
            if name in wanted:
                schema=layout(t,wanted[name][2])
                if schema:
                    size,fields=schema
                    if regions[wanted[name][0]]['file']!='port/data/vu1_micro.data.s' and fields and all(w==1 and path.startswith('[') for pos,w,k,path in fields):
                        schema=(size,[(pos,w,'opaque',path) for pos,w,k,path in fields])
                    matches.append((name,schema,row['source']))
        return matches
    witness=out/'layout-witness.o'
    witness_source=Path(__file__).resolve().parents[1]/'schema/layout_witness.c'
    subprocess.run([str(cc),'-mcpu=cell','-mhard-float','-g','-O0','-I'+str(root/'include'),'-I'+str(root/'port/include'),'-I'+str(root/'port/src'),'-include',str(root/'port/include/port_compat.h'),'-c',str(witness_source),'-o',str(witness)],check=True)
    jobs.append({'source':'engine/schema/layout_witness.c','object':str(witness)})
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for matches in pool.map(unit,jobs):
            for name,schema,source in matches:
                candidates.setdefault(name,[]).append((schema,source));layouts_seen+=1
    reviewed=reviewed_types(regions,raw)
    unused,unused_proof=unused_audit(regions,with_link_objects(report["files"],build))
    (out/"unused-native-proof.json").write_text(json.dumps(unused_proof,indent=2)+"\n")
    ops=[];gaps=[];pointer_errors=[];evidence=[];zeroed_unknown=[];alignment_prefixes=[]
    for region in regions:
        rid=region['id'];limit=region['size'];source=region['source_offset'];which=region['source_file'];cells={};conflicts=set();pointers={}
        for name,off in region['labels'].items():
            for (size,fields),unit_source in candidates.get(name,[]):
                if off+size>limit:continue
                evidence.append({'region':rid,'symbol':name,'unit':unit_source,'size':size})
                for pos,width,kind,path in fields:
                    at=off+pos
                    if at+width>limit:continue
                    cell=(width,kind)
                    for j in range(at,at+width):
                        old=cells.get(j)
                        item=(at,*cell)
                        if old and old!=item:conflicts.update(range(min(at,old[0]),max(at+width,old[0]+old[1])))
                        else:cells[j]=item
        # Reviewed canonical records override incomplete/alias DIEs only inside
        # the exact field extents proved by the public readers. The schema
        # builder has validated all counts and pointer graph bounds first.
        for spec in reviewed:
            if spec['region']!=rid:continue
            lo=spec['offset'];hi=lo+spec['size']
            for pos in list(cells):
                at,width,typ=cells[pos]
                if at<hi and lo<at+width:
                    if at<lo or at+width>hi:raise ValueError('reviewed schema splits a compiler field')
                    del cells[pos]
            conflicts.difference_update(range(lo,hi))
            for off,width,typ in spec['fields']:
                at=lo+off
                for j in range(at,at+width):cells[j]=(at,width,typ)
            evidence.append(spec)
        # gen_data.convert inserts addr&15 bytes BEFORE its first label.
        # Those bytes preserve alignment, and are not part of a PS2 object.
        prefix=min(region['labels'].values())
        first_address=(0x100000 if which==0 else 0x334c00)+source+prefix
        if prefix and (prefix!=first_address%16 or prefix>=16):
            raise ValueError('unproved generated alignment prefix')
        if prefix:alignment_prefixes.append({'region':rid,'offset':0,'length':prefix,
            'reason':'gen_data.py convert pre-label addr&15 alignment; preserved, not source data'})
        def resolve(value):
            if value==0:return (None,0)
            choices=reverse.get(value,[])
            if choices:
                # Same original address aliases must still identify one native destination.
                values={native[n][0] for n in choices}
                if len(values)!=1:return None
                return (sorted(choices)[0],0)
            if any(x['source_address']<=value<x['source_address']+x['length'] for x in unused):return None
            for target in regions:
                oldbase=(0x100000 if target['source_file']==0 else 0x334c00)+target['source_offset']
                if oldbase<=value<oldbase+target['size']:
                    delta=value-oldbase
                    return (target['anchor'],delta-target['anchor_offset'])
            return None
        for at,length,kind,expression in region['runs']:
            if at+length>limit:raise ValueError('cut crosses a run')
            if at<prefix:
                if at+length>prefix:raise ValueError('alignment prefix crosses first label')
                continue
            data=raw[which][source+at:source+at+length]
            if kind=='zero':
                if any(data):zeroed_unknown.append({'region':rid,'offset':at,'length':length,'reason':'upstream skeleton suppresses source bytes; needs semantic review'})
                continue
            if kind=='pointer':
                m=re.fullmatch(r'(\w+)([+-]\d+)?',expression);assert m
                symbol=m[1];addend=int(m[2] or 0);old=original_symbol(symbol)
                value=struct.unpack('<I',data)[0]
                if symbol not in native or old is None or value!=(old+addend)&0xffffffff or (addend and native[symbol][2]&15==2):
                    pointer_errors.append({'region':rid,'offset':at,'target':expression,'reason':'native/source symbolic binding not confirmed'});continue
                ops.append({'region':rid,'offset':at,'length':4,'width':4,'kind':'pointer','expected':value,'target':symbol,'addend':addend});continue
            pos=at
            while pos<at+length:
                cell=cells.get(pos)
                if cell and cell[0]==pos and not any(j in conflicts for j in range(pos,pos+cell[1])) and pos+cell[1]<=at+length:
                    _,width,typ=cell;value_bytes=raw[which][source+pos:source+pos+width]
                    op={'region':rid,'offset':pos,'length':width,'width':width,'kind':typ}
                    if typ in ('data_pointer','function_pointer'):
                        value=struct.unpack('<I',value_bytes)[0];binding=resolve(value)
                        if binding is None:typ='unmapped_pointer'
                        else:op.update(kind='pointer',expected=value,target=binding[0],addend=binding[1])
                    if typ=='padding' and any(value_bytes):typ='nonzero_padding'
                    if typ not in ('opaque','unmapped_pointer','nonzero_padding'):
                        ops.append(op);pos+=width;continue
                # Unknown bytes are NOT copied or word-swapped on a guess.
                start=pos;pos+=1
                while pos<at+length and (pos not in cells or pos in conflicts or cells[pos][0]!=pos or cells[pos][2]=='opaque'):pos+=1
                gaps.append({'region':rid,'offset':start,'length':pos-start,'reason':'missing/conflicting/opaque PPC type'})
    # All-zero representations are endian invariant even when C field types
    # are unresolved. This is a checked ZERO operation, never a guessed width.
    # Keep these distinct from compiler-typed fields in the evidence/report.
    safe_zero_bytes=0;nonzero_gaps=[]
    for gap in gaps:
        rg=regions[gap['region']];start=rg['source_offset']+gap['offset']
        value=raw[rg['source_file']][start:start+gap['length']]
        if not any(value):
            ops.append({'region':gap['region'],'offset':gap['offset'],'length':gap['length'],'width':1,'kind':'zero_invariant'})
            safe_zero_bytes+=gap['length']
        else:nonzero_gaps.append(gap)
    gaps=[]
    for gap in nonzero_gaps:
        excluded=[x for x in unused if x['region']==gap['region'] and x['offset']<=gap['offset'] and gap['offset']+gap['length']<=x['offset']+x['length']]
        if not excluded:gaps.append(gap)
    for gap in gaps+zeroed_unknown:
        rg=regions[gap['region']]
        names_before=[name for name,off in rg['labels'].items() if off<=gap['offset']]
        nearest=max(names_before,key=lambda name:rg['labels'][name]) if names_before else rg['anchor']
        gap.update(symbol=nearest,symbol_offset=gap['offset']-rg['labels'][nearest],source_address=(0x100000 if rg['source_file']==0 else 0x334c00)+rg['source_offset']+gap['offset'])
    for op in ops:
        if op['kind']=='pointer' and any(x['source_address']<=op['expected']<x['source_address']+x['length'] for x in unused):
            raise ValueError('live pointer into reviewed unused pool')
    # Excluded alignment must never hide a live symbolic data pointer.
    for op in ops:
        if op['kind']!='pointer':continue
        for target in regions:
            base=(0x100000 if target['source_file']==0 else 0x334c00)+target['source_offset']
            if base<=op['expected']<base+min(target['labels'].values()):
                raise ValueError('live source pointer into generated alignment prefix')
    # Compact adjacent equal-width scalar ops; pointers retain distinct checked bindings.
    compact=[]
    for op in sorted(ops,key=lambda x:(x['region'],x['offset'])):
        if compact and op['kind'] in ('scalar','padding','zero_invariant') and all(op[k]==compact[-1][k] for k in ['region','kind','width']) and compact[-1]['offset']+compact[-1]['length']==op['offset']:
            compact[-1]['length']+=op['length']
        else:compact.append(op.copy())
    for region in regions:
        region['copy_bytes']=sum(n for at,n,k,_ in region['runs'] if k!='zero' and at>=min(region['labels'].values()))
        region['typed_bytes']=sum(op['length'] for op in compact if op['region']==region['id'])
        region['sha256']=hashlib.sha256(raw[region['source_file']][region['source_offset']:region['source_offset']+region['size']]).hexdigest()
    result={'version':1,'profile':{'fighters':[0,1],'costumes':[0,0],'stage':0,'split_screen':False,'alternate_voice':False},'upstream':report['upstream'],'source_program_sha256':[hashlib.sha256(elf).hexdigest(),hashlib.sha256(raw[1]).hexdigest()],
        'safe_zero_invariant_bytes':safe_zero_bytes,'layout_evidence':'actual PPC DWARF via binutils readelf; no host sizeof or resource-content type inference',
        'complete':not gaps and not pointer_errors and not zeroed_unknown,'native_unused_ranges':unused,'native_unused_proof':'unused-native-proof.json','reviewed_schemas':reviewed,'alignment_prefixes':alignment_prefixes,'suppressed_source_bytes':zeroed_unknown,'regions':regions,'operations':compact,'untyped':gaps,'unresolved_pointer_bindings':pointer_errors,'type_evidence':evidence,'compiled_variable_layouts':layouts_seen,
        'copied_game_values_in_artifact':False,'battle_boot_ready':False}
    # Hash exactly the original bytes each op will consume, not whole commercial files.
    hv=0xcbf29ce484222325
    for op in compact:
        rg=regions[op['region']];start=rg['source_offset']+op['offset']
        for byte in raw[rg['source_file']][start:start+op['length']]:hv=((hv^byte)*0x100000001b3)&0xffffffffffffffff
    result['operation_source_fnv64']=f'{hv:016x}'
    # Only symbolic references enter the compiled catalog. Relinking cannot
    # leave old absolute link addresses in pointer fixups or destinations.
    declarations={r['anchor'] for r in regions}|{op['target'] for op in compact if op.get('target')}
    code=['/* Generated typed metadata only; no commercial data bytes. */','#include "xb_typed_data.h"']
    code += ['extern unsigned char '+name+'[];' for name in sorted(declarations)]
    code += ['static const XbTypedRegion regions[]={']
    for r in regions:
        code.append('{'+r['anchor']+'-'+str(r['anchor_offset'])+','+','.join(str(r[k]) for k in ['size','source_file','source_offset'])+'},')
    code += ['};','static const XbTypedOp ops[]={']
    for op in compact:
        value='((uintptr_t)'+op['target']+'+(intptr_t)('+str(op['addend'])+'))' if op.get('target') else '0'
        op['native_value_in_reference_link']=(native[op['target']][0]+op['addend']) if op.get('target') else 0
        kind=1 if op['kind']=='pointer' else 2 if op['kind'] in ('padding','zero_invariant') else 0
        code.append('{'+','.join(str(v) for v in [op['region'],op['offset'],op['length'],op['width'],kind,op.get('expected',0)])+','+value+'},')
    code += ['};',f'const XbTypedCatalog gXbTypedMainCatalog={{1,{int(result["complete"])},regions,{len(regions)},ops,{len(compact)},0x{hv:016x}ull}};']
    (out/'typed_catalog.c').write_text('\n'.join(code)+'\n')
    (out/'typed-manifest.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'regions':len(regions),'operations':len(compact),'typed_bytes':sum(x['typed_bytes'] for x in regions),'copy_bytes':sum(x['copy_bytes'] for x in regions),'gaps':len(gaps),'pointer_binding_errors':len(pointer_errors),'complete':result['complete']},indent=2))
if __name__=='__main__':main()
