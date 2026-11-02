#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Catalogue ALL resident native menu writable sections and their relocations.

No PS2 data types/addresses are guessed. C initializers and the native linker
produce the bytes/pointers that reload restores. Original MIPS code is identity
input only. Unmodelled relocation types are a build error, not zero-filled.
"""
from pathlib import Path
import struct,json,subprocess,hashlib

def read_object(path):
    b=path.read_bytes()
    if b[:6]!=b'\x7fELF\x01\x02':raise ValueError('expected ELF32-BE')
    h=struct.unpack_from('>16sHHIIIIIHHHHHH',b)
    sections=[struct.unpack_from('>10I',b,h[6]+i*h[11]) for i in range(h[12])]
    def blob(s):return b[s[4]:s[4]+s[5]]
    def string(s,off):return s[off:s.index(0,off)].decode()
    strings=blob(sections[h[13]])
    names=[string(strings,s[0]) for s in sections]
    symtab=next(s for s in sections if s[1]==2);symnames=blob(sections[symtab[6]])
    symbols=[]
    for at in range(symtab[4],symtab[4]+symtab[5],16):
        n,value,size,info,other,shndx=struct.unpack_from('>IIIBBH',b,at)
        symbols.append((string(symnames,n),value,size,info,shndx))
    return b,h,sections,names,symbols

def generate(rows,cc,sdk,out):
    chosen=[r for r in rows if r['source'].startswith('src/menu/') and r['compiled']]
    if not chosen or any(r['functions_without_C'] for r in chosen):raise ValueError('incomplete native menu C')
    gen=out/'overlay';gen.mkdir(exist_ok=True)
    regions=[];relocs=[];objects=[];decls=set();aliases=[];data=[];bss=[]
    objcopy=cc.with_name(cc.name[:-3]+'objcopy')
    for unit,row in enumerate(chosen):
        path=Path(row['object']);before,h,ss,names,syms=read_object(path)
        owned={};options=[]
        for i,s in enumerate(ss):
            if s[2]&3!=3 or not s[5]:continue
            if s[1] not in (1,8) or s[2]&0x400 or s[2]&4:raise ValueError('unsupported menu writable section '+names[i])
            key=len(regions);owned[i]=key
            regions.append({'object':row['source'],'section':names[i],'bytes':s[5],'kind':'zero' if s[1]==8 else 'initialized','policy':2 if row['source'].startswith('src/menu/port_') else 1})
            start=f'__xbo_start_{key}';end=f'__xbo_end_{key}'
            selector=f' . = ALIGN({max(1,s[8])}); {start} = .; KEEP("{path}"({names[i]})) {end} = .;'
            (bss if s[1]==8 else data).append(selector)
        common=[s for s in syms if s[4]==0xfff2]
        for symbol in common:
            # COMMON can be coalesced with a definition from another object;
            # its native symbol, not an input-file location counter, owns it.
            name,value,size,binding,shndx=symbol
            if any(r.get('common_symbol')==name for r in regions):
                if any(r.get('common_symbol')==name and r['bytes']!=size for r in regions):raise ValueError('ambiguous common size')
                continue
            key=len(regions);regions.append({'object':row['source'],'section':'COMMON','bytes':size,'common_symbol':name,'policy':2 if row['source'].startswith('src/menu/port_') else 1})
            bss.append(f' __xbo_start_{key} = {name}; __xbo_end_{key} = {name}+{size};')
        exported={}
        for s in ss:
            if s[1] not in (4,9) or s[7] not in owned:continue
            if s[1]!=4 or s[9]!=12:raise ValueError('expected PPC RELA')
            for at in range(s[4],s[4]+s[5],12):
                offset,info,addend=struct.unpack_from('>IIi',before,at)
                if info&255!=1:raise ValueError('only native R_PPC_ADDR32 data relocations supported')
                number=info>>8;name,value,size,binding,shndx=syms[number]
                if binding>>4==0:
                    if not 0<shndx<len(ss):raise ValueError('bad local relocation')
                    if number not in exported:
                        alias=f'__xbo_target_{unit}_{number}';exported[number]=alias
                        options+=['--add-symbol',f'{alias}={names[shndx]}:{value},global']
                        aliases.append({'object':row['source'],'alias':alias,'section':names[shndx],'value':value})
                    name=exported[number]
                if not name:raise ValueError('unnamed external relocation')
                decls.add(name)
                relocs.append({'region':owned[s[7]],'offset':offset,'symbol':name,'addend':addend,'type':1})
        if options:
            dest=path.with_suffix('.catalog.o')
            subprocess.run([str(objcopy),*options,str(path),str(dest)],check=True)
            after,hh,sss,nn,_=read_object(dest)
            if hh[7]!=h[7] or nn!=names:raise ValueError('objcopy changed ABI/sections')
            for old,new in zip(ss,sss):
                if old[2]&2 and (old[1:4]!=new[1:4] or old[5]!=new[5] or
                    old[1]!=8 and before[old[4]:old[4]+old[5]]!=after[new[4]:new[4]+new[5]]):raise ValueError('objcopy changed allocated payload')
            dest.replace(path)
        objects.append({'source':row['source'],'source_sha256':row['source_sha256'],'object_sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    # All mutable sections are now covered, not just sections reachable from a
    # single function. Read-only constants/code remain resident native ELF data.
    script=(sdk/'src/xsdk.ld').read_text()
    for anchor,insert in [('\t.data : {','\n\t\t__xbo_data_begin = .;\n'+'\n'.join(data)),('\t.bss : {','\n'+'\n'.join(bss))]:
        if script.count(anchor)!=1:raise ValueError('unexpected SDK linker script')
        script=script.replace(anchor,anchor+insert)
    anchor='*(.sbss .sbss.* .bss .bss.* COMMON)'
    script=script.replace(anchor,anchor+'\n\t\t__xbo_data_end = .;')
    ld=gen/'overlay.ld';ld.write_text(script)
    lines=['/* Generated from native objects, no game bytes. */','#include "xb_overlay.h"','extern void Progress_Main(int);','extern unsigned char __xbo_data_begin[],__xbo_data_end[];']
    for i,r in enumerate(regions):
        lines.append(f'extern unsigned char __xbo_start_{i}[],__xbo_end_{i}[];')
        if r['bytes'] is None:lines.append(f'extern unsigned char __xbo_size_{i}[];')
    for name in sorted(decls-{'Progress_Main'}):lines.append(f'extern unsigned char {name}[];')
    lines+=['static const XbOverlayRegion regions[]={']
    for i,r in enumerate(regions):
        size=str(r['bytes']) if r['bytes'] is not None else f'(size_t)__xbo_size_{i}'
        lines.append(f'{{__xbo_start_{i},__xbo_end_{i},{size},{r["policy"]}}},')
    lines+=['};','static const XbOverlayReloc relocs[]={']
    for r in relocs:lines.append(f'{{__xbo_start_{r["region"]}+{r["offset"]},((uintptr_t){r["symbol"]}+(intptr_t)({r["addend"]})),4}},')
    lines+=['};',f'const XbOverlayCatalog gXbOverlayCatalog={{1,{len(chosen)},regions,{len(regions)},relocs,{len(relocs)},__xbo_data_begin,__xbo_data_end,563996,0x91a321108fc31289ull,Progress_Main}};']
    source=gen/'overlay_catalog.c';source.write_text('\n'.join(lines)+'\n')
    (gen/'catalog.json').write_text(json.dumps({'native_menu_units':len(chosen),'regions':regions,'relocations':relocs,'exported_local_targets':aliases,'objects':objects,'source_identity':'USA DBZP.BIN, size+FNV-1a, not a cryptographic authenticity check','MIPS_bytes_copied_to_native_memory':False,'scope':'all compiled native menu writable sections/common symbols and their native pointer relocations; immutable constants and machine code stay resident under native linker control'},indent=2)+'\n')
    return source,ld
