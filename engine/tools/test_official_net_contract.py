#!/usr/bin/env python3
"""Wire/digest checks against official PC source; no game or Xbox execution."""
from pathlib import Path
import argparse,ctypes as C,json,os,re,socket,struct,subprocess
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[2];up=a.upstream.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
net=(up/'port/src/gs/net.c').read_text();assert 'NET_VERSION = 6' in net and 'IN_HEAD = 44' in net
s=(up/'port/src/headless.c').read_text();start=s.index('unsigned long long Port_NetDigest(void) {');i=s.index('{',start)+1;depth=1
while depth:
    depth+=(s[i]=='{')-(s[i]=='}');i+=1
reference=s[start:i]  # unchanged official function, compiled with value providers
fixture=r'''
#include "bt3_sync.h"
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef struct {int mode;unsigned flags;} Progress;
typedef struct {int state;} Seq;
static Progress progress,*gProgress;static Seq seq,*gBtlSeq;
static unsigned sNetArrivedAt,sBattles,sFromMenu,random_state=0x12345678;
static int tick,hp[2];static uint32_t positions[2][3];
static int *BtlSeq_GetClock(void){return &tick;}
static int BtlCharApi_GetHp(int k){return hp[k];}
static void BtlCharApi_GetPos(int k,float *p){memset(p,0,16);memcpy(p,positions[k],12);}
''' + reference + r'''
static uint32_t next(void){random_state=random_state*1664525u+1013904223u;return random_state;}
int main(void){
 for(unsigned n=0;n<2048;n++){
  Bt3SyncState state={0};sNetArrivedAt=next();progress.mode=n%9;progress.flags=(n&1)?0x40:0;
  gProgress=(n&4)?NULL:&progress;seq.state=n%7;gBtlSeq=(n&8)?NULL:&seq;
  sBattles=n&16;sFromMenu=n&32;tick=(int)next();
  for(unsigned k=0;k<2;k++){hp[k]=(int)next();for(unsigned j=0;j<3;j++)positions[k][j]=next();}
  state.arrived=sNetArrivedAt;
  if(gProgress){state.mode=progress.mode;state.first=(progress.flags&0x40)!=0;}
  if((sBattles || sFromMenu) && gBtlSeq && seq.state>=2){
   state.battle=1;state.sequence=seq.state;state.clock=tick;
   memcpy(state.hp,hp,sizeof(hp));memcpy(state.position_bits,positions,sizeof(positions));
  }
  uint64_t hash;assert(!bt3_sync_digest(&state,&hash) && hash==Port_NetDigest());
 }
 uint64_t untouched=42;assert(bt3_sync_digest(NULL,&untouched)==-1 && untouched==42);
 puts("PASS: 2048 digests match unchanged official Port_NetDigest with stubbed semantic providers; null refused");
}
'''
src=out/'official_digest_fixture.c';src.write_text(fixture)
common=['gcc','-std=c17','-O1','-g','-Wall','-Wextra','-Werror','-I'+str(root/'engine/net')]
exe=out/'digest-check'
subprocess.run([*common,'-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie',str(src),str(root/'engine/net/bt3_sync.c'),'-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'));(out/'digest.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
library=out/'wire.so';subprocess.run([*common,'-shared','-fPIC',str(root/'engine/net/bt3_wire.c'),'-o',str(library)],check=True)
lib=C.CDLL(str(library));lib.bt3_wire_encode.argtypes=lib.bt3_wire_decode.argtypes=[C.c_void_p,C.c_void_p,C.c_size_t,C.c_int]
lib.bt3_wire_encode.restype=lib.bt3_wire_decode.restype=C.c_size_t
def convert(data,encode=True):
    src=C.create_string_buffer(data);dst=C.create_string_buffer(len(data));fn=lib.bt3_wire_encode if encode else lib.bt3_wire_decode
    assert fn(dst,src,len(data),1)==len(data);return dst.raw
packets=0
for count in range(17):
    pads=bytes((i*17+3)&255 for i in range(count*18));head=(0x4e335442,3,100,count,0xfffffff0,0x12345678,3,4,64)
    native=struct.pack('>9IQ',*head,0x123456789abcdef0)+pads
    expected=struct.pack('<9IQ',*head,0x123456789abcdef0)+pads
    assert convert(native)==expected and convert(expected,False)==native;packets+=1
# Real two-way UDP only on loopback. Intentionally omit one in four windows;
# the next delivered 16-pad window must recover each pad byte.
sent=0;recovered={};inputs=[bytes((i+j*13)&255 for j in range(18)) for i in range(128)]
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as xbox,socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as pc:
    xbox.bind(('127.0.0.1',0));pc.bind(('127.0.0.1',0));xbox.settimeout(2);pc.settimeout(2)
    for i in range(128):
        if i%4==2:continue
        count=min(i+1,16);base=i+1-count;head=(0x4e335442,3,base,count,0,1000+i,0,0,0)
        native=struct.pack('>9IQ',*head,0)+b''.join(inputs[base:i+1]);xbox.sendto(convert(native),pc.getsockname())
        wire,peer=pc.recvfrom(2048);assert peer==xbox.getsockname();fields=struct.unpack_from('<9IQ',wire);assert fields[:2]==head[:2]
        assert len(wire)==44+fields[3]*18
        for j in range(fields[3]):recovered[fields[2]+j]=wire[44+j*18:44+(j+1)*18]
        fp=struct.pack('<5I',0x4e335442,12,0x46505231,0x55667788,0x11223344);pc.sendto(fp,peer)
        received,peer=xbox.recvfrom(2048);assert peer==pc.getsockname()
        assert convert(received,False)==struct.pack('>5I',0x4e335442,12,0x46505231,0x55667788,0x11223344)
        sent+=1
assert len(recovered)==128 and all(recovered[i]==inputs[i] for i in range(128))
report={'result':'PASS','official_commit':subprocess.check_output(['git','-C',str(up),'rev-parse','HEAD'],text=True).strip(),'wire_protocol_seen':6,'T_INPUT_count_variants':packets,'official_digest_comparisons':2048,'UDP_loopback_packets_sent':sent,'UDP_windows_intentionally_omitted':32,'pad_records_recovered':128,'simulated_big_endian_native_to_PC_LE':True,'full_game_executed':False,'Xbox_executed':False,'cross_architecture_game_determinism_proven':False,'rollback_implemented':False,'lobby_or_matchmaking_connected':False}
(out/'contract.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
