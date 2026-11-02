#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Reviewed types for data whose labels have no complete C variable DIE.
Schemas follow named public readers/headers. No global byte/word heuristics.
Every dynamic count/pointer is bounded before a schema is accepted.
"""
import struct,math

def describe(regions,raw):
    specs=[];byname={n:(rg,off) for rg in regions for n,off in rg['labels'].items()}
    def address(name):
        rg,off=byname[name];assert rg['source_file']==0
        return 0x100000+rg['source_offset']+off
    def locate(addr,n):
        for rg in regions:
            base=(0x100000 if rg['source_file']==0 else 0x334c00)+rg['source_offset']
            if base<=addr and addr+n<=base+rg['size']:
                return rg,addr-base
        raise ValueError(f'reviewed type outside catalog: {addr:x}+{n:x}')
    def data(addr,n):
        rg,off=locate(addr,n);at=rg['source_offset']+off
        return raw[rg['source_file']][at:at+n]
    def u32(addr):return struct.unpack('<I',data(addr,4))[0]
    def schema(addr,size,fields,evidence):
        rg,off=locate(addr,size)
        if any(o<0 or o+w>size or w not in (1,2,4,8) for o,w,k in fields):raise ValueError('bad reviewed schema')
        specs.append({'region':rg['id'],'offset':off,'size':size,'fields':fields,'evidence':evidence})
    def array(addr,size,width,evidence,kind='scalar'):
        assert size%width==0
        schema(addr,size,[(i,width,kind) for i in range(0,size,width)],evidence)
    def record_array(addr,count,size,fields,evidence):
        assert 0<count<=4096
        schema(addr,count*size,[(i*size+o,w,k) for i in range(count) for o,w,k in fields],evidence)
    S='scalar';P='data_pointer';F='function_pointer'
    # Header documents these as eight four-byte RGBA colours; consumer keeps u8*.
    array(address('gEftBladeColor'),32,1,'battle/eft_aa.h EftBlade.color + eft_aa.c EftBlade_Init')
    aim=[(0,4,S)]+[(j,1,S) for j in range(4,8)]
    for name,n in [('D_002C4620',8),('D_002C4660',10)]:
        a=address(name)
        if any(u32(a+i*8)>6 for i in range(n)):raise ValueError('unknown volley opcode')
        record_array(a,n,8,aim,'battle/eft_det_a.h EftVolleyAimStep; eft_det_a.c steering script reader')
    # Traverse actual pause-menu descriptors, not arbitrary label lengths.
    menu=[(0,4,P),(4,4,S),(8,4,S),(12,2,S),(14,2,S)]+[(j,4,S) for j in range(16,52,4)]
    item=[(0,2,S),(2,2,S),(4,4,S),(8,4,P),(12,4,F)]+[(j,4,S) for j in range(16,48,4)]
    queue=[address(n) for n in byname if n.startswith('gPauseMenu') and 'Cursor' not in n and n!='gPauseMenuWhich']
    seen=set();arrays=set()
    while queue:
        a=queue.pop()
        if a in seen:continue
        if not 0x2c4e70<=a<0x2c6070:raise ValueError('pause descriptor outside documented span')
        seen.add(a);ptr,count=u32(a),u32(a+4);scale=struct.unpack('<f',data(a+8,4))[0]
        if not 0<count<=64 or not math.isfinite(scale) or not 0<scale<=8:raise ValueError('pause descriptor count/scale')
        if ptr<0x2c4e70 or ptr+count*48>0x2c6070:raise ValueError('pause item array bounds')
        schema(a,52,menu,'battle/hud_0.h BtlMenu; hud_0_b.c menu reader')
        if (ptr,count) not in arrays:
            record_array(ptr,count,48,item,'battle/hud_0.h BtlMenuItem; hud_0_b.c item reader');arrays.add((ptr,count))
        for i in range(count):
            child=u32(ptr+i*48+8)
            if child:queue.append(child)
    # Font tag names are explicit UTF16 code units, not byte strings.
    for name in ['D_002C6430','D_002C6438','D_002C6440','D_002C6448']:
        array(address(name),8,2,'battle/col_c.h FontTagDef.name; col_c.c tag scanner')
    tag=[(0,4,P),(4,2,S),(6,1,'padding'),(7,1,'padding'),(8,4,F),(12,4,S)]
    record_array(address('gFontTags'),4,16,tag,'battle/col_c.h FontTagDef')
    # Tables are bounded by their next documented typed family.
    a=address('D_002C6490');end=address('D_002C6838');assert (end-a)%12==0
    name_fields=[(i,2,S) for i in range(0,8,2)]+[(8,4,S)]
    for i in range((end-a)//12):
        if u32(a+i*12+8)>4:raise ValueError('font icon name length')
    record_array(a,(end-a)//12,12,name_fields,'battle/col_c.h FontIconName; col_c_b.c name->chars/len')
    a=end;end=address('D_002C6988');assert (end-a)%12==0
    definition=[(i,1,S) for i in range(4)]+[(4,2,S)]+[(i,1,S) for i in range(6,12)]
    for i in range((end-a)//12):
        if struct.unpack('<H',data(a+i*12+4,2))[0]>6:raise ValueError('font icon frame count')
    record_array(a,(end-a)//12,12,definition,'battle/col_c.h FontIconDef; col_c_b.c GetFrame')
    table=address('gFontIconTables')
    for port in range(4):
        a=u32(table+port*4);n=0
        while True:
            locate(a+n*12,4)
            if not u32(a+n*12):
                schema(a+n*12,4,[(0,4,P)],'FontIconEntry sentinel: col_c_b.c name==NULL');break
            if n>=64:raise ValueError('unterminated icon table')
            n+=1
        if n:record_array(a,n,12,[(0,4,P),(4,4,P),(8,4,S)],'battle/col_c.h FontIconEntry')
    # Four full jaw keys followed by eight unused zero bytes, checked by loader.
    record_array(address('D_002C6E18'),4,16,[(i,4,S) for i in range(0,16,4)],'battle/bobj_a.h BobjJawKey; bobj_a.c jawKeys reader')
    # Explicit math objects; keep original numeric bit patterns.
    for name,n in [('gVu0SinCoef',4),('D_002EC260',16),('gVu0AxisZW',4)]:
        array(address(name),n*4,4,'sys/math3d.h Vec4/Mtx44; sys/vu0_* portable coefficient/axis counterparts')
    # These are byte-indexed AI condition codes, NOT unknown serialized structs.
    for name,n in [('D_002EDC70',112),('D_002EDCE0',112),('D_002EDD50',112),('D_002EDDC0',24),('D_002EDDD8',56),('D_002EDE10',160),('D_002EDEB0',56),('D_002EDEE8',32)]:
        array(address(name),n,1,'btl_ai_cond.c explicit u8 code arrays and byte-indexed consumers; btl_ai_think.h')
    # C-string consumers are explicit: OverlayEntry and file/ADX APIs.
    strings=[('D_002EB380',24),('D_002EB398',16),('D_002F3490',16),('D_002F34A0',16),('D_002F34B0',16),('gFileDirName',16),('D_002F34D0',16),('D_002F34E0',16),('D_002F34F0',16),('gFileRootDir',8),('gMovieTagIdle',8),('gMovieTagNoData',8),('gMovieTagFull',8),('D_002FF130',8),('D_002FF138',8),('D_002FF140',8),('D_002FF148',8)]
    for name,n in strings:
        value=data(address(name),n)
        if b'\0' not in value:raise ValueError('unterminated reviewed C string '+name)
        array(address(name),n,1,'main.h OverlayEntry, sys/file.c char directories, sys/movie.c char tags, sys/adx.c channel names')
    # Original 0x108B80 LWC1 then 0x108B84 CVT.S.W: integer 224,
    # addressed by LUI at 0x108B54 + ADDIU at 0x108B78. Only four
    # bytes are read; the remaining four are checked endian-invariant zero.
    array(address('D_002FE8D0'),4,4,'SLUS USA 0x108B54/78/80/84; gfxm_b.c GfxWater_DrawView integer h converted to float')
    a=address('D_002FE8D0')+4
    if any(data(a,4)):raise ValueError('water constant trailing padding changed')
    array(a,4,1,'water constant: checked trailing zero padding',kind='padding')
    return specs
