#!/usr/bin/env python3
"""GS texture decoder checked against actual upstream GS core; no game assets."""
import ctypes as C,json,pathlib,random,struct,subprocess,sys
engine=pathlib.Path(__file__).resolve().parents[1];repo=pathlib.Path(sys.argv[1]).resolve()
work=pathlib.Path(sys.argv[2]).resolve();work.mkdir(parents=True,exist_ok=True)
source=work/'reference.c'
source.write_text('#include "'+str(repo/'port/src/gs/gs_core.c')+'"\n'+r'''
void reference_init(const unsigned char *bytes) {
 tables_init();
 for(unsigned i=0;i<(1u<<20);i++) sVram[i]=(uint32_t)bytes[i*4]|(uint32_t)bytes[i*4+1]<<8|(uint32_t)bytes[i*4+2]<<16|(uint32_t)bytes[i*4+3]<<24;
}
unsigned reference_read(unsigned bp,unsigned bw,unsigned psm,unsigned x,unsigned y) {return Gs_VramRead(bp,bw,psm,x,y);}
void reference_write(unsigned bp,unsigned bw,unsigned psm,unsigned x,unsigned y,unsigned v) {vram_rw(bp,bw,psm,x,y,1,v);}
unsigned reference_texel(unsigned bp,unsigned bw,unsigned psm,unsigned cbp,unsigned cpsm,unsigned x,unsigned y,unsigned ta0,unsigned ta1,unsigned aem) {
 gGs.tex0[0]=(uint64_t)bp|(uint64_t)bw<<14|(uint64_t)psm<<20|(uint64_t)10<<26|(uint64_t)10<<30|(uint64_t)cbp<<37|(uint64_t)cpsm<<51;
 gGs.clamp[0]=0;gGs.texa=(uint64_t)ta0|(uint64_t)ta1<<32|(uint64_t)aem<<15;
 return texel(0,x,y);
}
''')
(work/'exports.map').write_text('{global: reference_*; dr_gs_*; local: *;};\n')
subprocess.run(['gcc','-std=gnu17','-O2','-fPIC','-shared','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-Wl,--version-script='+str(work/'exports.map'),'-Wl,-z,defs',str(source),str(engine/'graphics/dr_gs_texture.c'),'-I'+str(repo/'port/src/gs'),'-lm','-o',str(work/'graphics.so')],check=True)
lib=C.CDLL(str(work/'graphics.so'))
class Texture(C.Structure):_fields_=[(n,C.c_uint32) for n in ['width','height','tbp','tbw','psm','cbp','cpsm','csm','ta0','ta1','aem']]
lib.dr_gs_raw_read.argtypes=[C.c_void_p,C.c_size_t,*[C.c_uint32]*5,C.POINTER(C.c_uint32)]
lib.dr_gs_texture_decode.argtypes=[C.c_void_p,C.c_size_t,C.POINTER(Texture),C.c_void_p,C.c_size_t]
lib.dr_gs_address.argtypes=[*[C.c_uint32]*5,C.POINTER(C.c_size_t),C.POINTER(C.c_uint),C.POINTER(C.c_uint)]
lib.reference_init.argtypes=[C.c_void_p]
lib.reference_read.argtypes=[C.c_uint]*5;lib.reference_read.restype=C.c_uint
lib.reference_texel.argtypes=[C.c_uint]*10;lib.reference_texel.restype=C.c_uint
rng=random.Random(360);raw=rng.randbytes(4*1024*1024);vram=C.create_string_buffer(raw)
lib.reference_init(vram);assert lib.dr_gs_texture_selftest()==1
formats=[0,1,2,0xa,0x13,0x14,0x1b,0x24,0x2c]
for psm in formats:
 for i in range(3000):
  bp=rng.randrange(16384);bw=rng.randrange(64);x=rng.randrange(4096);y=rng.randrange(4096)
  value=C.c_uint32();assert lib.dr_gs_raw_read(vram,len(raw),bp,bw,psm,x,y,C.byref(value))==0
  assert value.value==lib.reference_read(bp,bw,psm,x,y),(psm,bp,bw,x,y)
 for cpsm in ([0,2,0xa] if psm in [0x13,0x14,0x1b,0x24,0x2c] else [0]):
  for aem in [0,1]:
   t=Texture(32,32,100,4,psm,1000,cpsm,0,0x20,0x80,aem)
   out=C.create_string_buffer(32*32*4)
   assert lib.dr_gs_texture_decode(vram,len(raw),C.byref(t),out,len(out))==0
   for y in range(32):
    for x in range(32):
     expected=lib.reference_texel(t.tbp,t.tbw,t.psm,t.cbp,t.cpsm,x,y,t.ta0,t.ta1,t.aem)
     assert out.raw[(y*32+x)*4:(y*32+x+1)*4]==struct.pack('<I',expected),(psm,cpsm,x,y)
# Page and column addressing must cover each page bijectively for plain formats.
for psm,w,h in [(0,64,32),(2,64,64),(0xa,64,64),(0x13,128,64),(0x14,128,128)]:
 seen=set()
 for y in range(h):
  for x in range(w):
   off=C.c_size_t();shift=C.c_uint();bits=C.c_uint()
   assert not lib.dr_gs_address(0,2,psm,x,y,C.byref(off),C.byref(shift),C.byref(bits))
   assert (off.value,shift.value) not in seen;seen.add((off.value,shift.value))
 assert len(seen)==w*h
# Unsupported and invalid modes leave output untouched.
t=Texture(32,32,0,2,0x13,1000,0,1,0,128,0);out=C.create_string_buffer(b'Q'*4096)
assert lib.dr_gs_texture_decode(vram,len(raw),C.byref(t),out,4096)!=0 and out.raw[:4096]==b'Q'*4096
t.csm=0
assert lib.dr_gs_texture_decode(vram,len(raw),C.byref(t),out,4095)!=0 and out.raw[:4096]==b'Q'*4096
assert lib.dr_gs_texture_decode(vram,4095,C.byref(t),out,4096)!=0
report={'raw_GS_samples':27000,'decoded_pixels_against_original_core':9*0,'formats':formats,'coverage':'32/24/16/16S/8/4/8H/4HL/4HH, CSM1 RGBA32/16/16S, TEXA/AEM, 4MiB wrap, bijective page addressing','limitations':'CSM2, CLUT latch state, mipmaps and real game textures not validated; no GPU execution'}
report['decoded_pixels_against_original_core']= (4*1+5*3)*2*32*32
(work/'graphics-tests.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
