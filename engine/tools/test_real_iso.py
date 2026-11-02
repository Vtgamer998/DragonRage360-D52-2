#!/usr/bin/env python3
"""Read-only validation of real resources using the project's actual C modules.
No commercial bytes are extracted to the report. No GPU or game execution.
Usage: test_real_iso.py game.iso pinned-upstream output-directory
"""
import ctypes as C
import hashlib
import importlib.util
import json
import pathlib
import struct
import subprocess
import sys
import time

engine=pathlib.Path(__file__).resolve().parents[1]
iso=pathlib.Path(sys.argv[1]).resolve()
repo=pathlib.Path(sys.argv[2]).resolve()
out=pathlib.Path(sys.argv[3]).resolve();out.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('original_fingerprint',repo/'port/tools/make_fingerprint.py')
fp=importlib.util.module_from_spec(spec);spec.loader.exec_module(fp)
sources=['dr_disc.c','dr_container.c','dr_bpe.c','dr_anim.c','dr_skeleton.c','dr_face_vif.c']
subprocess.run(['gcc','-std=c17','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC','-Wl,-z,defs',
                '-I'+str(engine/'resource'),*[str(engine/'resource'/s) for s in sources],
                '-o',str(out/'resources.so')],check=True)
lib=C.CDLL(str(out/'resources.so'))
Read=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint64,C.c_void_p,C.c_size_t)
class Span(C.Structure):
    _fields_=[('offset',C.c_uint64),('size',C.c_uint64),('directory',C.c_uint)]
class Disc(C.Structure):
    _fields_=[('read',Read),('opaque',C.c_void_p),('size',C.c_uint64),('root',Span)]
lib.dr_mount.argtypes=[C.POINTER(Disc),Read,C.c_void_p,C.c_uint64]
for name in ['dr_find','dr_game_file']:
    getattr(lib,name).argtypes=[C.POINTER(Disc),C.c_char_p,C.POINTER(Span)]
lib.dr_afs_entry.argtypes=[C.POINTER(Disc),C.POINTER(Span),C.c_uint32,C.POINTER(Span)]
lib.dr_pack_memory_entry.argtypes=[C.c_void_p,C.c_size_t,C.c_uint32,C.POINTER(C.c_size_t),C.POINTER(C.c_size_t),C.POINTER(C.c_uint)]
lib.dr_bpe_decode.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)]
lib.dr_anim_prepare.argtypes=[C.c_void_p,C.c_size_t,C.c_int]
lib.dr_skeleton_prepare.argtypes=[C.c_void_p,C.c_size_t,C.c_int,C.POINTER(C.c_size_t)]
class FaceStats(C.Structure):
    _fields_=[(n,C.c_uint32) for n in ['commands','unpacks','vectors','continues','noops']]+[
        ('dma_payload_bytes',C.c_size_t),('guard_offset',C.c_size_t)]
lib.dr_face_vif_parse.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.c_void_p,C.POINTER(FaceStats),C.POINTER(C.c_size_t)]
f=iso.open('rb');size=iso.stat().st_size
@Read
def read(_,at,where,n):
    try:
        f.seek(at);data=f.read(n)
        if len(data)!=n:return -1
        C.memmove(where,data,n);return 0
    except (OSError,ValueError):return -1
d=Disc();assert lib.dr_mount(C.byref(d),read,None,size)==0
report={'input_bytes':size,'environment':'host CPU, little-endian',
        'upstream':subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip(),
        'afs':[], 'animations':{'packs':0,'nonempty':0,'empty':0,'decoded_empty':0,'decoded':0,'validated':0,
            'big_endian_semantic_matches':0,'original_BPE_reference_matches':0,'failures':0,'examples':[]},
        'models':{'packs':0,'empty_files':0,'absent_skin_placeholders':0,'pmdl':0,'skeletons':0,'nodes':0,'core64_semantic_matches':0,
            'texture_file_structures':0,'unclassified_model_resources':0,'face_geometry_lists':0,
            'face_vif_unpacks':0,'face_vif_vectors':0,'face_vif_continues':0,'face_vif_noops':0,
            'failures':0,'examples':[]},
        'limits':['not Xbox/XEX/PowerPC execution','not GPU rasterization','no combat loop or AI execution',
                  'skeleton core 64 bytes only; bounds, model headers, VU streams and skinning remain unconverted',
                  'texture packet structure validation only, no decoded real-texture pixel claim',
                  'face DMA/VIF is parsed, MSCNT microprograms are not executed',
                  'no online/rollback/NAT test','no FPS measurement']}
def fail(section,**details):
    section['failures']+=1
    if len(section['examples'])<24:section['examples'].append(details)
def game_file(fid):
    span=Span();rc=lib.dr_game_file(C.byref(d),f'pzs3us1/{fid-1:05d}.bin'.encode(),C.byref(span))
    assert rc==0,(fid,rc)
    f.seek(span.offset);data=f.read(span.size);assert len(data)==span.size
    return data
def entries(data):
    table=fp.pack_strict_table(data)
    if table is None:return None
    buf=C.create_string_buffer(data)
    for index,(a,b,flags) in enumerate(table):
        off=C.c_size_t();length=C.c_size_t();tag=C.c_uint()
        rc=lib.dr_pack_memory_entry(buf,len(data),index,C.byref(off),C.byref(length),C.byref(tag))
        if a==b:assert rc==-4
        else:assert rc==0 and (off.value,length.value,tag.value)==(a,b-a,flags)
    return [(a,b,data[a:b]) for a,b,_ in table]
def animation_signature(data,endian):
    def words(at,n):return struct.unpack_from(endian+str(n)+'I',data,at)
    def halves(at,n):return struct.unpack_from(endian+str(n)+'H',data,at)
    header=halves(2,73);events=[];tracks=[]
    for i in range(data[1]):
        at=0x94+16*i;event=(*words(at,2),*halves(at+8,2),*words(at+12,1))
        shape=words(event[3]*4,7) if event[0]&1 else ()
        events.append((event,shape))
    for offset in halves(6,71):
        if not offset:tracks.append(None);continue
        at=offset*4;flags,count=halves(at,2)
        keys=(words(at+4,count*2),halves(at+4+count*8,count)) if flags&1 else words(at+4,count*6)
        tracks.append((flags,count,keys))
    return (data[0],data[1],header,tuple(events),tuple(tracks))
for i in range(3):
    span=Span();name=f'DATA/PZS3US{i}.AFS'
    assert lib.dr_find(C.byref(d),name.encode(),C.byref(span))==0
    f.seek(span.offset);header=f.read(8);assert header[:4]==b'AFS\0'
    count=struct.unpack_from('<I',header,4)[0]
    for j in range(count):
        child=Span();assert lib.dr_afs_entry(C.byref(d),C.byref(span),j,C.byref(child))==0
    report['afs'].append({'name':name,'bytes':span.size,'bounded_entries':count})
decoded=C.create_string_buffer(0xc000)
start=time.monotonic()
for character in range(fp.CHARS):
    fid=0x598+character*10;data=game_file(fid);rows=entries(data);a=report['animations'];a['packs']+=1
    if rows is None:fail(a,character=character,fid=fid,stage='pack table');continue
    # The real loader uses motion-file IDs below 0x19E; later IDs come from models.
    for index,(_,_,payload) in enumerate(rows[:0x19e]):
        if not payload:a['empty']+=1;continue
        a['nonempty']+=1;buf=C.create_string_buffer(payload);written=C.c_size_t()
        rc=lib.dr_bpe_decode(buf,len(payload),decoded,len(decoded),C.byref(written))
        if rc:fail(a,character=character,index=index,stage='BPE',error=rc);continue
        a['decoded']+=1;raw=decoded.raw[:written.value]
        if not raw:
            # Motion catalogs contain BPE streams that expand to no motion.
            # This is an empty slot, not a successfully validated BobjAnim.
            a['decoded_empty']+=1;continue
        if index<2 or character==0 and index<16:
            reference=fp.bpe_decode(payload)
            if reference!=raw:fail(a,character=character,index=index,stage='original BPE comparison');continue
            a['original_BPE_reference_matches']+=1
        rc=lib.dr_anim_prepare(decoded,len(raw),0)
        if rc:fail(a,character=character,index=index,stage='animation validation',error=rc);continue
        assert decoded.raw[:len(raw)]==raw
        a['validated']+=1
        rc=lib.dr_anim_prepare(decoded,len(raw),1)
        if rc or animation_signature(raw,'<')!=animation_signature(decoded.raw[:len(raw)],'>'):
            fail(a,character=character,index=index,stage='big-endian semantics',error=rc);continue
        a['big_endian_semantic_matches']+=1
    m=report['models']
    for skin in range(8):
        fid=0x590+character*10+skin;data=game_file(fid)
        if not data:m['empty_files']+=1;continue
        rows=entries(data);m['packs']+=1
        if rows is None:fail(m,character=character,skin=skin,stage='pack table');continue
        if data==struct.pack('<3I',1,64,64)+bytes(52):
            # Exact observed empty-skin marker. Other short packs still fail.
            m['absent_skin_placeholders']+=1;continue
        if len(rows)<3:fail(m,character=character,skin=skin,stage='missing model entry');continue
        model=rows[2][2]
        if not model:continue
        if len(model)<0x70 or model[:4]!=b'pmdl':
            fail(m,character=character,skin=skin,stage='pmdl header');continue
        m['pmdl']+=1;offset=struct.unpack_from('<I',model,0x6c)[0]
        if offset<0x70 or offset>=len(model):
            fail(m,character=character,skin=skin,stage='node offset');continue
        source=model[offset:];buf=C.create_string_buffer(source);nodes=C.c_size_t()
        rc=lib.dr_skeleton_prepare(buf,len(source),1,C.byref(nodes))
        if rc:fail(m,character=character,skin=skin,stage='skeleton',error=rc);continue
        at=0;seen=0
        while True:
            le=struct.unpack_from('<I4H13I',source,at);be=struct.unpack_from('>I4H13I',buf.raw,at)
            assert le==be
            seen+=1
            if le[2]:break
            at+=le[0]
        assert seen==nodes.value
        m['skeletons']+=1;m['nodes']+=nodes.value;m['core64_semantic_matches']+=seen
        for index,(_,_,payload) in enumerate(rows):
            if index==0 or 3<=index<=16:
                if not payload:continue
                if fp.is_tex(payload):m['texture_file_structures']+=1
                else:
                    stats=FaceStats();bad=C.c_size_t();buf=C.create_string_buffer(payload)
                    rc=lib.dr_face_vif_parse(buf,len(payload),None,None,C.byref(stats),C.byref(bad))
                    if rc:
                        m['unclassified_model_resources']+=1
                        fail(m,character=character,skin=skin,index=index,stage='face DMA/VIF',error=rc,offset=bad.value)
                    else:
                        m['face_geometry_lists']+=1;m['face_vif_unpacks']+=stats.unpacks
                        m['face_vif_vectors']+=stats.vectors;m['face_vif_continues']+=stats.continues
                        m['face_vif_noops']+=stats.noops
    if character%16==0:
        print(json.dumps({'character':character,'validated_animations':a['validated'],'failures':a['failures']+m['failures']}),flush=True)
report['seconds']=round(time.monotonic()-start,2)
report['source_sha256']={s:hashlib.sha256((engine/'resource'/s).read_bytes()).hexdigest() for s in sources}
report['result']='PASS' if not report['animations']['failures'] and not report['models']['failures'] else 'FAIL'
(out/'real-iso-results.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2),flush=True)
sys.exit(0 if report['result']=='PASS' else 1)
