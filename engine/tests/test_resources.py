#!/usr/bin/env python3
"""Synthetic format and real upstream function tests; no ISO/assets redistributed."""
import ctypes as C, json, pathlib, random, struct, subprocess, sys
engine=pathlib.Path(__file__).resolve().parents[1]
repo=pathlib.Path(sys.argv[1]).resolve()
work=pathlib.Path(sys.argv[2]).resolve();work.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(repo/'port/tools'))
import portsrc,eeconst
prepared,_,_=portsrc.prepare(repo/'src/battle/bobj_a.c')
flags=['-std=gnu99','-O2','-w','-fno-builtin','-fno-strict-aliasing','-fPIC','-ffunction-sections','-fdata-sections','-DDR_XBOX360','-include',str(repo/'port/include/port_compat.h'),'-I'+str(repo/'include'),'-I'+str(repo/'port/include'),'-I'+str(engine/'resource')]
pre=subprocess.check_output(['gcc',*flags,'-E',str(prepared)],text=True)
source=work/'bobj.i';source.write_text(eeconst.transform(pre))
subprocess.run(['gcc',*flags,'-fpreprocessed','-c',str(source),'-o',str(work/'bobj.o')],check=True)
(work/'exports.map').write_text('{ global: dr_*; resource_*; Bpe_Decode; local: *; };\n')
# Only retained game functions are linked: absent renderer/memory layers remain absent.
subprocess.run(['gcc',*flags,'-shared','-Wl,--gc-sections','-Wl,--version-script='+str(work/'exports.map'),'-Wl,-z,defs',str(work/'bobj.o'),str(repo/'src/sys/mem_read.c'),str(repo/'src/sys/bpe.c'),str(engine/'tests/resource_game_probe.c'),*[str(p) for p in (engine/'resource').glob('*.c')],'-o',str(work/'resources.so')],check=True)
lib=C.CDLL(str(work/'resources.so'))
lib.dr_bpe_decode.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)]
lib.dr_anim_prepare.argtypes=[C.c_void_p,C.c_size_t,C.c_int]
lib.Bpe_Decode.argtypes=[C.c_void_p,C.c_void_p,C.POINTER(C.c_int32)];lib.Bpe_Decode.restype=C.c_void_p
lib.resource_game_probe.argtypes=[C.c_void_p];lib.resource_mem_probe.argtypes=[C.c_void_p]

def pack(raw):
    blocks=b''
    for start in range(0,len(raw),65535):
        part=raw[start:start+65535]
        blocks+=bytes([254,127,254,255])+struct.pack('>H',len(part))+part
    return struct.pack('<II',len(raw),len(blocks))+blocks

def decode(p,cap):
    src=C.create_string_buffer(p);out=C.create_string_buffer(max(1,cap));n=C.c_size_t(99)
    error=lib.dr_bpe_decode(src,len(p),out,cap,C.byref(n))
    return error,out.raw[:n.value],n.value

assert lib.dr_resource_selftest()==1
assert lib.resource_mem_probe(C.create_string_buffer(b'!'+struct.pack('<I H f',0x89abcdef,0xfffe,-2.5)))==1
rng=random.Random(360)
for n in [0,1,255,1024,49152,70000]+[rng.randrange(1,2000) for _ in range(50)]:
    raw=rng.randbytes(n);packed=pack(raw)
    assert decode(packed,n)==(0,raw,n)
    reference=C.create_string_buffer(max(1,n));size=C.c_int32()
    assert lib.Bpe_Decode(C.create_string_buffer(packed),reference,C.byref(size))
    assert size.value==n and reference.raw[:n]==raw
# A non-identity dictionary: token 200 expands to 'AB', including nested token 201.
def dictionary(pairs):
    table=b''
    for base in [0,128]:
        table+=bytes([127])
        for i in range(base,base+128):
            table+=bytes(pairs[i]) if i in pairs else bytes([i])
    return table
block=dictionary({200:(65,66),201:(200,200)})+b'\x00\x02'+bytes([201,200])
p=struct.pack('<II',6,len(block))+block
assert decode(p,6)==(0,b'ABABAB',6)
reference=C.create_string_buffer(6);size=C.c_int32()
lib.Bpe_Decode(C.create_string_buffer(p),reference,C.byref(size));assert reference.raw==b'ABABAB'
for n in range(len(p)):
    assert decode(p[:n],6)[0]!=0
assert decode(p,5)[0]!=0
assert decode(struct.pack('<II',7,len(block))+block,7)[0]!=0
# Self-referential dictionary must terminate with an error.
cycle=dictionary({200:(201,65),201:(200,66)})+b'\x00\x01'+bytes([200])
assert decode(struct.pack('<II',1,len(cycle))+cycle,1)[0]!=0

# Real motion format: 2 hit events, a full track, a rotation-only track, shared shape.
a=bytearray(300);a[1]=2
struct.pack_into('<HH',a,2,30,0x1234)
struct.pack_into('<HHH',a,6,60,67,60) # track 0 and 2 share one record
struct.pack_into('<IIHHI',a,148,1,1,5,45,8)
struct.pack_into('<IIHHI',a,164,2,0,10,0,0)
struct.pack_into('<7f',a,180,1.25,-2.5,3.75,0,2,3,4)
struct.pack_into('<HH6I',a,240,0,1,0x3f800000,0xc0200000,0x7fc01234,0x12345678,0x11223344,0x55667788)
struct.pack_into('<HHIIH',a,268,1,1,0xabcdef01,0x23456789,0x1234)
expected=bytearray(a)
for off in range(2,148,2):expected[off:off+2]=a[off:off+2][::-1]
for e in [148,164]:
    for off,n in [(e,4),(e+4,4),(e+8,2),(e+10,2),(e+12,4)]:expected[off:off+n]=a[off:off+n][::-1]
for off in range(180,208,4):expected[off:off+4]=a[off:off+4][::-1]
for off,n in [(240,2),(242,2),(268,2),(270,2),(280,2)]+[(x,4) for x in list(range(244,268,4))+[272,276]]:expected[off:off+n]=a[off:off+n][::-1]
b=C.create_string_buffer(bytes(a));assert lib.dr_anim_prepare(b,len(a),1)==0 and b.raw[:len(a)]==expected
b=C.create_string_buffer(bytes(a));assert lib.dr_anim_prepare(b,len(a),0)==0 and b.raw[:len(a)]==a
# Upstream evaluator reads the decoded native-endian fixture and its hit volume.
error,raw,_=decode(pack(a),len(a));assert not error
assert lib.resource_game_probe(C.create_string_buffer(raw))==1
for field,value in [(6,0xffff),(6,1),(6,62),(242,0),(270,0xffff),(158,0xffff)]:
    broken=bytearray(a);struct.pack_into('<H',broken,field,value)
    b=C.create_string_buffer(bytes(broken));assert lib.dr_anim_prepare(b,len(broken),1)!=0
    assert b.raw[:len(broken)]==broken
for n in range(148):
    b=C.create_string_buffer(bytes(a[:n]));assert lib.dr_anim_prepare(b,n,1)!=0
# Every byte prefix and 2000 deterministic corruptions: no crashes, bounded execution.
for n in range(len(a)):
    b=C.create_string_buffer(bytes(a[:n]));lib.dr_anim_prepare(b,n,1)
for _ in range(2000):
    broken=bytearray(a)
    for j in range(rng.randrange(1,7)):broken[rng.randrange(len(broken))]=rng.randrange(256)
    b=C.create_string_buffer(bytes(broken));lib.dr_anim_prepare(b,len(broken),1)
report={'BPE':'56 random/edge reference vectors + pair expansion; truncated/cyclic/capacity failures', 'motion':'full and rotation-only tracks, shared track, events, hit volume, exact big-endian bytes, float/quat bits, unchanged failure buffer, 2000 corruptions','game_logic':'actual upstream BtlObjAnim_UpdateEvents tested: hit opens, shape scaled, hit closes','PowerPC_runtime':'not executed; compile check separate','real_ISO':'not tested'}
(work/'resources-tests.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
