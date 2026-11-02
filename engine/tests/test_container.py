#!/usr/bin/env python3
"""End-to-end synthetic ISO -> AFS -> character BIN -> animation; no game assets."""
import ctypes as C,json,pathlib,struct,sys
engine=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(engine.parent/'DragonRage360-Demo/tests'))
from test_disc import fixture,dual32,SECTOR
lib=C.CDLL(str(pathlib.Path(sys.argv[1]).resolve()))
Read=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint64,C.c_void_p,C.c_size_t)
class Span(C.Structure):_fields_=[('offset',C.c_uint64),('size',C.c_uint64),('directory',C.c_uint)]
class Disc(C.Structure):_fields_=[('read',Read),('opaque',C.c_void_p),('size',C.c_uint64),('root',Span)]
lib.dr_mount.argtypes=[C.POINTER(Disc),Read,C.c_void_p,C.c_uint64]
lib.dr_game_file.argtypes=[C.POINTER(Disc),C.c_char_p,C.POINTER(Span)]
lib.dr_pack_entry.argtypes=[C.POINTER(Disc),C.POINTER(Span),C.c_uint32,C.POINTER(Span),C.POINTER(C.c_uint)]
lib.dr_pack_memory_entry.argtypes=[C.c_void_p,C.c_size_t,C.c_uint32,C.POINTER(C.c_size_t),C.POINTER(C.c_size_t),C.POINTER(C.c_uint)]
lib.dr_pack_memory_span.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.POINTER(C.c_size_t)]
lib.dr_load_motion.argtypes=[C.POINTER(Disc),C.POINTER(Span),C.c_int,C.c_int,C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)]
lib.dr_motion_from_memory.argtypes=[C.c_void_p,C.c_size_t,C.c_int,C.c_int,C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)]
lib.dr_skeleton_prepare.argtypes=[C.c_void_p,C.c_size_t,C.c_int,C.POINTER(C.c_size_t)]
lib.resource_file_probe.argtypes=[C.c_void_p,C.c_int,C.c_int]
assert lib.dr_container_selftest()==1
raw=bytearray(208);raw[1]=2;struct.pack_into('<HH',raw,2,30,0x1234)
struct.pack_into('<IIHHI',raw,148,1,1,5,45,8)
struct.pack_into('<IIHHI',raw,164,2,0,10,0,0)
struct.pack_into('<7f',raw,180,1.25,-2.5,3.75,0,2,3,4)
payload=bytes([254,127,254,255])+struct.pack('>H',len(raw))+raw
bpe=struct.pack('<II',len(raw),len(payload))+payload
pack=struct.pack('<III',1,12|3,12+((len(bpe)+3)&~3))+bpe+b'\0'*((-len(bpe))%4)
image=fixture();afs_size=64+len(pack)
image[22*SECTOR+68+10:22*SECTOR+68+18]=dual32(afs_size)
image[26*SECTOR:26*SECTOR+16]=b'AFS\0'+struct.pack('<III',1,64,len(pack))
image[26*SECTOR+64:26*SECTOR+64+len(pack)]=pack
reads=[]
@Read
def read(_,offset,out,n):
    reads.append((offset,n));assert offset+n<=len(image)
    C.memmove(out,bytes(image[offset:offset+n]),n);return 0
D=Disc();assert lib.dr_mount(C.byref(D),read,None,len(image))==0
P=Span();assert lib.dr_game_file(C.byref(D),b'pzs3us0/00000.bin',C.byref(P))==0
E=Span();flags=C.c_uint()
assert lib.dr_pack_entry(C.byref(D),C.byref(P),0,C.byref(E),C.byref(flags))==0 and flags.value==3
assert E.offset==26*SECTOR+64+12 and E.size==len(pack)-12
scratch=C.create_string_buffer(256);out=C.create_string_buffer(len(raw));n=C.c_size_t()
assert lib.dr_load_motion(C.byref(D),C.byref(E),1,1,scratch,256,out,len(raw),C.byref(n))==0
assert n.value==len(raw) and out.raw[2:6]==b'\x00\x1e\x12\x34'
# Evaluate actual original hit windows from the ISO-decoded native-endian motion.
lib.resource_game_probe.argtypes=[C.c_void_p]
assert lib.dr_load_motion(C.byref(D),C.byref(E),1,0,scratch,256,out,len(raw),C.byref(n))==0
assert lib.resource_game_probe(out)==1
# Exact span: cannot borrow bytes from the surrounding ISO, even if present.
short=Span(E.offset,10,0);reads.clear()
assert lib.dr_load_motion(C.byref(D),C.byref(short),1,1,scratch,256,out,len(raw),C.byref(n))!=0 and n.value==0
assert all(at+length<=short.offset+short.size for at,length in reads)
assert lib.dr_load_motion(C.byref(D),C.byref(E),1,1,scratch,10,out,len(raw),C.byref(n))!=0
assert lib.dr_load_motion(C.byref(D),C.byref(E),1,1,scratch,256,out,100,C.byref(n))!=0
# Original file lookup uses index 1, while the container API uses index 0.
buf=C.create_string_buffer(pack);off=C.c_size_t();size=C.c_size_t()
assert lib.resource_file_probe(buf,len(pack),12)==1
assert lib.dr_pack_memory_entry(buf,len(pack),0,C.byref(off),C.byref(size),C.byref(flags))==0
assert (off.value,size.value)==(12,len(pack)-12)
assert lib.dr_pack_memory_span(buf,len(pack),C.byref(buf,12),C.byref(size))==0
assert lib.dr_pack_memory_span(buf,len(pack),C.byref(buf,13),C.byref(size))!=0
for corrupt in [struct.pack('<III',1,8,16)+b'X'*8,struct.pack('<IIII',2,20,16,24)+b'X'*8,struct.pack('<III',0xffffffff,12,16),struct.pack('<III',1,12,0xfffffff0)]:
    b=C.create_string_buffer(corrupt)
    assert lib.dr_pack_memory_entry(b,len(corrupt),0,C.byref(off),C.byref(size),None)!=0
empty=C.create_string_buffer(struct.pack('<III',1,12,12))
assert lib.dr_pack_memory_entry(empty,12,0,C.byref(off),C.byref(size),None)!=0
assert lib.dr_pack_memory_entry(buf,len(pack),1,C.byref(off),C.byref(size),None)!=0
for length in range(len(pack)):
    assert lib.dr_pack_memory_entry(buf,length,0,C.byref(off),C.byref(size),None)!=0
# Embedded, uncompressed model motion: copy and convert without mutating the source.
b=C.create_string_buffer(bytes(raw));before=b.raw
assert lib.dr_motion_from_memory(b,len(raw),0,1,out,len(raw),C.byref(n))==0 and b.raw==before
assert out.raw[2:6]==b'\x00\x1e\x12\x34'
# Skeleton links are relative byte offsets; gaps and unknown fields stay intact.
bones=bytearray(208)
for i,at in enumerate([0,80,144]):
    struct.pack_into('<I4HI',bones,at,[80,64,0][i],i,1 if i==2 else 0,0x1234,42+i,0xabcdef01)
    struct.pack_into('<12I',bones,at+16,*[0x3f800000+j+i for j in range(12)])
expected=bytearray(bones)
for at in [0,80,144]:
    for local,width in [(0,4),(4,2),(6,2),(8,2),(10,2)]+[(j,4) for j in range(12,64,4)]:
        start=at+local;expected[start:start+width]=bones[start:start+width][::-1]
b=C.create_string_buffer(bytes(bones));nodes=C.c_size_t()
assert lib.dr_skeleton_prepare(b,len(bones),1,C.byref(nodes))==0 and nodes.value==3
assert b.raw[:len(bones)]==expected
for local,value in [(0,0),(0,4),(0,0xfffffff0),(80,0)]:
    broken=bytearray(bones);struct.pack_into('<I',broken,local,value);b=C.create_string_buffer(bytes(broken));before=b.raw
    assert lib.dr_skeleton_prepare(b,len(broken),1,C.byref(nodes))!=0 and nodes.value==0 and b.raw==before
for length in range(208):
    b=C.create_string_buffer(bytes(bones));before=b.raw
    assert lib.dr_skeleton_prepare(b,length,1,C.byref(nodes))!=0 and b.raw==before
report={'pipeline':'PASS synthetic ISO9660 -> AFS exact entry -> strict character BIN -> bounded BPE -> big-endian motion','bounds':'entry truncation cannot borrow adjacent bytes; malformed/empty/oversized/nonmonotonic packs rejected; capacities enforced','upstream':'actual BObjFile_GetEntry and actual BtlObjAnim_UpdateEvents tested; ISO-decoded synthetic hit opens/scales/closes','skeleton':'3 linked nodes with gaps, exact BE fields, truncated/invalid link rejection before mutation','embedded_motion':'uncompressed copy-and-convert API passed; PlayModel tested separately in 32-bit i386, no Xbox run','real_ISO_and_Xbox':'not tested'}
pathlib.Path(sys.argv[1]).with_name('container-tests.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
