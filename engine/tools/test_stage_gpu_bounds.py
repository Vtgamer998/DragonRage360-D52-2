#!/usr/bin/env python3
"""Build a transient VU fixture from the user's USA program; distribute no game bytes."""
import argparse,struct,subprocess,os,hashlib,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('program',type=Path);p.add_argument('output',type=Path);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
b=a.program.read_bytes();phoff=struct.unpack_from('<I',b,28)[0];ents,num=struct.unpack_from('<HH',b,42);payload=None
for i in range(num):
 typ,off,va,pa,fs,ms,flags,align=struct.unpack_from('<8I',b,phoff+i*ents)
 if typ==1 and va<=0x2c04f0 and 0x2c1180<=va+fs:payload=b[off+0x2c04f0-va:off+0x2c1180-va]
assert payload is not None
micro=bytearray(16384);i=0;end=0
while i<len(payload):
 code=struct.unpack_from('<I',payload,i)[0];i+=4
 if not code:continue
 assert (code>>24)&127==0x4a
 n=(code>>16)&255;n=n or 256;addr=(code&65535)*8
 micro[addr:addr+n*8]=payload[i:i+n*8];i+=n*8;end=max(end,addr+n*8)
assert end==3192
fixture=bytes(micro[:end]);assert hashlib.sha256(fixture).hexdigest()=='91fe696613836cf30368398b8ea71d011a91a0bda7c9561d8dd03307ac339eff'
f=out/'private-vu.bin';f.write_bytes(fixture);engine=Path(__file__).resolve().parents[1]
subprocess.run(['gcc','-std=gnu17','-O1','-g','-fsanitize=address,undefined','-fno-pie','-no-pie',str(engine/'tests/stage_gpu_bounds.c'),'-lm','-o',str(out/'check')],check=True)
r=subprocess.run([str(out/'check'),str(f)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'));(out/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
