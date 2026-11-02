#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Read compiler-reported PPC layouts through binutils readelf.
No type is inferred from resource bytes, symbol names or PS2 pointer ranges.
Unresolved types, ambiguous unions/bitfields and opaque unk byte arrays block
coverage; they are never treated as words merely to make a manifest complete.
"""
import re,subprocess

def read(path,readelf):
    text=subprocess.check_output([str(readelf),'--debug-dump=info','--wide',str(path)],text=True)
    dies={};stack=[];current=None
    for line in text.splitlines():
        m=re.match(r'\s*<(\d+)><([0-9a-f]+)>: Abbrev Number: \d+(?: \((DW_TAG_\w+)\))?',line)
        if m:
            depth,off=int(m[1]),int(m[2],16)
            while len(stack)>depth:stack.pop()
            if not m[3]:current=None;continue
            current={'tag':m[3][7:],'attrs':{},'children':[]};dies[off]=current
            if stack:dies[stack[-1]]['children'].append(off)
            stack.append(off);continue
        m=re.search(r'DW_AT_(\w+)\s*:\s*(.*)',line)
        if m and current:current['attrs'][m[1]]=m[2]
    def attrs(off):
        a=dies[off]['attrs'];base=ref(a.get('specification',''))
        return dict(attrs(base),**a) if base in dies else a
    def ref(s):
        m=re.search(r'<0x([0-9a-f]+)>',s);return int(m[1],16) if m else None
    def number(s):
        m=re.fullmatch(r'(?:\([^)]*\)\s*)?(-?(?:0x[0-9a-f]+|\d+))',s)
        return int(m[1],0) if m else None
    def name(s):
        if '): ' in s:return s.rsplit('): ',1)[1]
        return re.sub(r'^\([^)]*\)\s*','',s)
    memo={}
    def layout(off,extent=None,trail=()):
        key=(off,extent)
        if key in memo:return memo[key]
        if off not in dies or off in trail:return None
        d=dies[off];a=attrs(off);tag=d['tag'];size=number(a.get('byte_size',''));target=ref(a.get('type',''));children=d['children'];result=None
        if tag in ('typedef','const_type','volatile_type','restrict_type'):
            return layout(target,extent,trail+(off,))
        if tag in ('base_type','enumeration_type') and size in (1,2,4,8):result=(size,[(0,size,'scalar','')])
        elif tag=='pointer_type' and size==4:
            t=target;seen=set()
            while t in dies and t not in seen and dies[t]['tag'] in ('typedef','const_type','volatile_type'):
                seen.add(t);t=ref(attrs(t).get('type',''))
            result=(4,[(0,4,'function_pointer' if t in dies and dies[t]['tag']=='subroutine_type' else 'data_pointer','')])
        elif tag=='array_type':
            item=layout(target,None,trail+(off,));counts=[]
            for ch in children:
                ca=attrs(ch)
                if dies[ch]['tag']=='subrange_type':
                    count=number(ca.get('count',''));upper=number(ca.get('upper_bound',''));lower=number(ca.get('lower_bound','')) or 0
                    counts.append(count if count is not None else upper-lower+1 if upper is not None else None)
            if item and counts:
                n=1
                for count in counts:
                    if count is None:
                        if len(counts)==1 and extent is not None and extent%item[0]==0:count=extent//item[0]
                        else:n=None;break
                    n*=count
                if n is not None and 0<n<=1048576:
                    result=(n*item[0],[(i*item[0]+o,w,k,f'[{i}]'+path) for i in range(n) for o,w,k,path in item[1]])
        elif tag in ('structure_type','union_type') and size is not None and size<=1048576:
            fields=[];variants=[];valid=True;covered=set()
            for ch in children:
                ca=attrs(ch)
                if dies[ch]['tag']!='member':continue
                nm=name(ca.get('name',''));pos=number(ca.get('data_member_location','')) if tag=='structure_type' else 0
                if 'bit_size' in ca or 'data_bit_offset' in ca or pos is None:valid=False;break
                child=layout(ref(ca.get('type','')),None,trail+(off,))
                if not child or pos+child[0]>size:valid=False;break
                parts=[]
                for o,w,k,path in child[1]:
                    if k=='scalar' and w==1 and nm.startswith(('unk','pad','reserved')):k='opaque'
                    parts.append((pos+o,w,k,'.'+nm+path))
                if tag=='union_type':variants.append(parts)
                else:
                    for i in range(pos,pos+child[0]):
                        if i in covered:valid=False
                        covered.add(i)
                    fields+=parts
            if tag=='union_type':
                if not variants or any([(o,w,k) for o,w,k,_ in v]!=[(o,w,k) for o,w,k,_ in variants[0]] for v in variants):valid=False
                else:fields=variants[0];covered={i for o,w,k,path in fields for i in range(o,o+w)}
            if valid:
                fields += [(i,1,'padding','.<padding>') for i in range(size) if i not in covered]
                result=(size,sorted(fields))
        memo[key]=result;return result
    variables=[]
    for off,d in dies.items():
        if d['tag']!='variable':continue
        a=attrs(off)
        if not number(a.get('external','')):continue
        nm=name(a.get('name',''))
        if nm:variables.append((nm,ref(a.get('type',''))))
    return variables,layout
