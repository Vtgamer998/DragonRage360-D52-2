# SPDX-License-Identifier: AGPL-3.0-or-later
"""Prove that three original pool spans are outside this native build's reads.
These bytes are NOT assigned invented C types, copied, or zero-filled.
Any new compiled relocation into a retired symbol fails regeneration. The
active vector is limited to its declared 16-byte views in pinned consumers.
"""
from pathlib import Path
import hashlib,struct,json
from build_overlay_catalog import read_object
POOLS=[('D_002EC2C0',16,16),('D_002FE958',0,22),('D_002FEB88',0,8)]
VECTOR_USERS={'src/battle/bobj_a.c','src/battle/eft_aa.c','src/battle/eft_det_a.c','src/battle/eft_det_b.c','src/battle/stg_a_b.c','src/battle/stg_a.c','src/sys/gfxm_d_c.c'}
VECTOR_SOURCE_HASHES={'src/sys/gfxm_d_c.c': '66c744ab7e87d410a31ade10cd28cd37e30f0116b406cc1b65c4e3dffc6a0823', 'src/battle/eft_det_a.c': 'd5f4875b3dfd4f9378234627ff7a8174e67a988b404f5ee1ca0f87224e37c6f3', 'src/battle/eft_aa.c': 'bbe82fd011e4b377206b94b5a1157e98091a41dff81ddfcc9f1a39887fa03e24', 'src/battle/eft_det_b.c': '6a3a7ea6e1e1c11737911725d3b1e27ab32a39d592f5c18b9d09f1764fb15b23', 'src/battle/stg_a_b.c': 'a522b8b1679141bb62c9f4633013a07a67dcf3752c3e22531e962ab7101d578f', 'src/battle/bobj_a.c': '70a8126f82a2a6159a3b58d466b3627463df81141024ab95efd5ebcc0f77a524', 'src/battle/stg_a.c': '43ced23f5b0e63a03bc749682dff35dc07791e5d964a387901927bef81ad5e63'}

def with_link_objects(rows,build):
    result=list(rows);seen={str(Path(r['object']).resolve()) for r in rows}
    for arg in json.loads((Path(build)/'link-arguments.json').read_text()):
        if not arg.endswith('.o'):continue
        path=Path(arg).resolve()
        if str(path) not in seen:
            result.append({'source':'native-title/'+path.name,'compiled':True,'object':str(path)})
            seen.add(str(path))
    return result

def audit(regions,rows):
    hits=[];byname={n:(r,o) for r in regions for n,o in r['labels'].items()}
    for row in rows:
        if not row['compiled']:raise ValueError('unused-data proof needs all native objects')
        if row['source'] in VECTOR_SOURCE_HASHES and row['source_sha256']!=VECTOR_SOURCE_HASHES[row['source']]:
            raise ValueError('changed vector consumer needs semantic review: '+row['source'])
        b,h,sections,names,symbols=read_object(Path(row['object']))
        for sec in sections:
            if sec[1]!=4 or not sections[sec[7]][2]&2:continue
            for at in range(sec[4],sec[4]+sec[5],12):
                loc,info,addend=struct.unpack_from('>IIi',b,at);target=symbols[info>>8][0]
                if target not in {p[0] for p in POOLS}:continue
                if target!='D_002EC2C0' or addend!=0 or row['source'] not in VECTOR_USERS:
                    raise ValueError('new native consumer of reviewed pool: '+str((row['source'],target,addend)))
                hits.append({'source':row['source'],'symbol':target,'addend':addend,'relocation':info&255})
    if not hits:raise ValueError('vector-reader evidence unexpectedly absent')
    result=[]
    for name,delta,size in POOLS:
        rg,off=byname[name];lo=off+delta
        if lo+size>rg['size']:raise ValueError('retired pool extent')
        result.append({'region':rg['id'],'offset':lo,'length':size,'symbol':name,'symbol_offset':delta,
         'source_address':0x100000+rg['source_offset']+lo,'reason':'no native pool consumer; active vector references restricted to declared 16-byte Vec4/StgColVec/EftDetVec readers' if delta else 'no allocated native relocation to this original-only pool symbol',
         'policy':'leave untouched; forbidden pointer target during catalog generation; not typed data'})
    return result,{'result':'PASS','all_compiled_units_checked':len(rows),'vector_relocations':hits,'pools':result,'scope':'pinned native build; not a claim about every PS2 reader or future resource schema'}
