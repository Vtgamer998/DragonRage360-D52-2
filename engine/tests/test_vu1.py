#!/usr/bin/env python3
"""Compare checked standalone VM against unmodified upstream on LE host.
No commercial microprograms/assets; PPC compile is separate from execution.
"""
import ctypes as C,json,pathlib,random,struct,subprocess,sys
engine=pathlib.Path(__file__).resolve().parents[1];repo=pathlib.Path(sys.argv[1]).resolve()
work=pathlib.Path(sys.argv[2]).resolve();work.mkdir(parents=True,exist_ok=True)
original=subprocess.check_output(['git','show','HEAD:port/src/gs/gs_vu1.c'],cwd=repo,text=True)
reference=work/'vu_reference.c'
reference.write_text(original+r'''
void Gs_Gif(const uint8_t *p,uint32_t n) {(void)p;(void)n;}
void reference_reset(void) {memset(&vu,0,sizeof(vu));memset(sUnknown,0,sizeof(sUnknown));vu.cl=vu.wl=1;vu.vf[0].f[3]=1;}
void reference_control(unsigned cl,unsigned wl,unsigned mode,unsigned mask,const uint32_t *row,const uint32_t *col) {
 GsVu1_SetCycle(cl,wl);GsVu1_SetMode(mode);GsVu1_SetMask(mask);GsVu1_SetRow(row);GsVu1_SetCol(col);
}
void reference_program(const void *p,unsigned n) {GsVu1_Program(0,p,n/4);}
void reference_unpack(unsigned cmd,unsigned num,unsigned imm,const void *p) {GsVu1_Unpack(cmd,num,imm,p);}
void reference_run(void) {run(0);}
void reference_read(void *p) {memcpy(p,vu.mem,sizeof(vu.mem));}
''')
(work/'exports.map').write_text('{global: dr_vu1_*; reference_*; local: *;};\n')
subprocess.run(['gcc','-std=gnu17','-O2','-ffp-contract=off','-fPIC','-ffunction-sections','-fdata-sections','-shared','-Wl,--gc-sections','-Wl,--version-script='+str(work/'exports.map'),'-Wl,-z,defs','-I'+str(repo/'port/src/gs'),'-I'+str(engine/'vu'),str(reference),str(engine/'vu/dr_vu1.c'),str(engine/'vu/dr_vu1_selftest.c'),'-lm','-o',str(work/'vu-tests.so')],check=True)
lib=C.CDLL(str(work/'vu-tests.so'))
lib.dr_vu1_reset.argtypes=[C.c_void_p,C.c_void_p]
lib.dr_vu1_control.argtypes=[*[C.c_uint32]*4,C.c_void_p,C.c_void_p]
lib.reference_control.argtypes=lib.dr_vu1_control.argtypes
lib.dr_vu1_unpack.argtypes=[C.c_uint32,C.c_uint32,C.c_uint32,C.c_void_p,C.c_size_t]
lib.reference_unpack.argtypes=[C.c_uint,C.c_uint,C.c_uint,C.c_void_p]
lib.dr_vu1_program.argtypes=[C.c_uint32,C.c_void_p,C.c_size_t]
lib.reference_program.argtypes=[C.c_void_p,C.c_uint]
lib.dr_vu1_call.argtypes=[C.c_int,C.c_uint32]
lib.dr_vu1_read.argtypes=[C.c_uint32,C.c_void_p,C.c_size_t]
lib.reference_read.argtypes=[C.c_void_p]
assert lib.dr_vu1_selftest()==1
rng=random.Random(360);comparisons=0
for i in range(1000):
 lib.dr_vu1_reset(None,None);lib.reference_reset()
 cl,wl=rng.randrange(1,5),rng.randrange(1,5);mode=rng.randrange(3);mask=rng.getrandbits(32)
 row=C.create_string_buffer(rng.randbytes(16));col=C.create_string_buffer(rng.randbytes(16))
 assert not lib.dr_vu1_control(cl,wl,mode,mask,row,col);lib.reference_control(cl,wl,mode,mask,row,col)
 for j in range(4):
  vn=rng.randrange(4);vl=rng.randrange(3);cmd=0x60|(vn<<2)|vl|(0x10 if rng.randrange(2) else 0)
  if rng.randrange(8)==0:cmd=0x6f # V4-5
  num=rng.randrange(1,17);imm=rng.randrange(1024)|(0x4000 if rng.randrange(2) else 0)
  # Oversized supplied span has deterministic padding, no pointer alignment requirement.
  payload=C.create_string_buffer(rng.randbytes(1025));ptr=C.byref(payload,1)
  assert lib.dr_vu1_unpack(cmd,num,imm,ptr,1024)==0
  lib.reference_unpack(cmd,num,imm,ptr)
  a=C.create_string_buffer(16384);b=C.create_string_buffer(16384)
  assert not lib.dr_vu1_read(0,a,16384);lib.reference_read(b);assert a.raw==b.raw,(i,j,cmd)
  comparisons+=1
# Synthetic scalar/vector arithmetic programs with LQ / SQ and all-component masks.
def inst(lo,hi):return struct.pack('<II',lo,hi)
def upper(op,fd,fs,ft):return 15<<21|ft<<16|fs<<11|fd<<6|op
for i in range(400):
 lib.dr_vu1_reset(None,None);lib.reference_reset()
 data=C.create_string_buffer(struct.pack('<8f',*[rng.uniform(-32,32) for _ in range(8)]))
 assert not lib.dr_vu1_unpack(0x6c,2,0,data,32);lib.reference_unpack(0x6c,2,0,data)
 op=rng.choice([0x28,0x2a,0x2c,0x2b,0x2f,0,1,2,3,0x18,0x19,0x1a,0x1b])
 code=inst(15<<21|1<<16,0x2ff)+inst(15<<21|2<<16|1,0x2ff)+inst(0x8000033c,upper(op,3,1,2))+inst(1<<25|15<<21|3<<11|2,0x2ff)+inst(0x8000033c,0x400002ff)+inst(0x8000033c,0x2ff)
 code=C.create_string_buffer(code);assert not lib.dr_vu1_program(0,code,48);lib.reference_program(code,48)
 assert not lib.dr_vu1_call(0,16);lib.reference_run()
 a=C.create_string_buffer(16384);b=C.create_string_buffer(16384)
 assert not lib.dr_vu1_read(0,a,16384);lib.reference_read(b);assert a.raw==b.raw
# Unsupported NaN FTOI must fail before a host-dependent/undefined integer cast.
lib.dr_vu1_reset(None,None)
nan_data=C.create_string_buffer(struct.pack('<4I',0x7fc00000,0x3f800000,0,0))
assert not lib.dr_vu1_unpack(0x6c,1,0,nan_data,16)
nan_code=inst(15<<21|1<<16,0x2ff)+inst(0x8000033c,upper(0x3c,5,1,2))+inst(0x8000033c,0x400002ff)+inst(0x8000033c,0x2ff)
assert not lib.dr_vu1_program(0,C.create_string_buffer(nan_code),len(nan_code))
assert lib.dr_vu1_call(0,16)==-200
# Wrapped XGKICK output remains canonical LE, including the tag's high bits.
packets=[]
Callback=C.CFUNCTYPE(None,C.c_void_p,C.c_void_p,C.c_uint32)
@Callback
def callback(_,p,n):packets.append(C.string_at(p,n*16))
lib.dr_vu1_reset(C.cast(callback,C.c_void_p),None)
packet=struct.pack('<QQ',0x1000000000008001,0)+b'PAYLOAD-16-BYTES'
assert len(packet)==32
assert not lib.dr_vu1_unpack(0x6c,2,1023,C.create_string_buffer(packet),32)
code=inst(8<<25|1<<16|1023,0x2ff)+inst(0x80000000|27<<6|0x3c|1<<11,0x2ff)+inst(0x8000033c,0x400002ff)+inst(0x8000033c,0x2ff)
assert not lib.dr_vu1_program(0,C.create_string_buffer(code),len(code))
assert not lib.dr_vu1_call(0,16) and packets==[packet]
# Bad command/short source/bad addresses are rejected before execution; instruction budget terminates loops.
lib.dr_vu1_reset(None,None);a=C.create_string_buffer(16384);before=a.raw
assert lib.dr_vu1_unpack(0x6c,1,0,C.create_string_buffer(b'X'*15),15)==-201
assert not lib.dr_vu1_read(0,a,16384) and a.raw==before
assert lib.dr_vu1_unpack(0x63,1,0,C.create_string_buffer(b'X'*16),16)==-200
assert lib.dr_vu1_program(2048,C.create_string_buffer(code),len(code))==-200
assert lib.dr_vu1_read(16380,a,8)==-200
loop=inst(0x20<<25|0x7ff,0x2ff)+inst(0x8000033c,0x2ff)
assert not lib.dr_vu1_program(0,C.create_string_buffer(loop),len(loop))
assert lib.dr_vu1_call(0,32)==-202
report={'unpack_comparisons':comparisons,'arithmetic_programs':400,'two_bone':'synthetic LQ/MULA/MADDA/MADD/SQ program -> (6,25,19.5,1)','GIF':'wrapped XGKICK exact canonical bytes passed','guards':'truncation, invalid formats/addresses, byte capacity, instruction budget, NaN FTOI rejection','reference':'actual unmodified upstream gs_vu1.c on little-endian host','PowerPC_execution':'not run','real_game_microprograms':'not tested; not full skinning/game integration'}
(work/'vu-tests.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
