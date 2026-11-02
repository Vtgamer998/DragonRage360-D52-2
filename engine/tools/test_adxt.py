#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare native ADX against actual pinned upstream decoder, with real disc resources."""
from pathlib import Path
import argparse,subprocess,struct,hashlib,os,json
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('xsdk',type=Path);p.add_argument('iso',type=Path);p.add_argument('output',type=Path);p.add_argument('--sanitize',action='store_true');a=p.parse_args()
e=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
src=a.upstream/'port/src/gs/snd_adx.c';text=src.read_text()
def function(name):
 start=text.index(name);start=text.rfind('\n',0,start)+1;br=text.index('{',start);depth=1;at=br+1
 while depth:
  if text[at]=='{':depth+=1
  if text[at]=='}':depth-=1
  at+=1
 return text[start:at]
header='''#include <stdint.h>
typedef struct {uint8_t *file;uint32_t size,data,channels,rate,total,pos,loopStart,loopEnd;int loop,c1,c2,hist[2][2],pan[2];} RefPlayer;
void Ref_coefs(uint32_t,uint32_t,int *,int *);
int Ref_decode(RefPlayer *,int16_t *,int);
'''
(out/'reference.h').write_text(header)
ref='#include "reference.h"\n#include <stddef.h>\n#include <math.h>\nstatic int sMono;\n#define PAN_AUTO (-128)\n#define FRAME 18\n'
ref+=function('static uint32_t be16(')+'\n'
ref+=function('static void adx_coefs(').replace('static void adx_coefs','void Ref_coefs')+'\n'
ref+=function('static int decode(').replace('static int decode(Player *','int Ref_decode(RefPlayer *')+'\n'
(out/'reference.c').write_text(ref)
# Validated USA image's AFS2; source bytes are temporary test inputs, not distributed.
iso=a.iso.open('rb');afs=1466093568;iso.seek(afs);magic,count=struct.unpack('<4sI',iso.read(8));assert magic==b'AFS\0'
table=[struct.unpack('<II',iso.read(8)) for _ in range(count)];chosen={}
for index,(offset,size) in enumerate(table):
 if size<0x40 or size>4*1024*1024:continue
 iso.seek(afs+offset);h=iso.read(0x40)
 if h[:2]!=b'\x80\0' or h[4:7]!=b'\x03\x12\x04' or h[7] not in (1,2) or h[0x12]!=4:continue
 rate,total=struct.unpack_from('>II',h,8)
 key=(rate,h[7])
 if rate in (16000,24000,48000) and total>=1024 and key not in chosen:
  iso.seek(afs+offset);data=iso.read(size);path=out/f'real-{rate}-{h[7]}.adx';path.write_bytes(data)
  chosen[key]={'file':str(path),'index':index,'size':size,'sha256':hashlib.sha256(data).hexdigest()}
 if len(chosen)>=6:break
iso.close();assert len(chosen)>=3
cmd=['gcc','-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-I'+str(e/'platform/platform'),'-I'+str(e.parent/'src'),'-I'+str(a.xsdk.resolve()/'include'),'-I'+str(out)]
if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
exe=out/'test-adxt';subprocess.run(cmd+[str(e/'tests/test_xb_adxt.c'),str(e/'platform/platform/xb_adxt.c'),str(out/'reference.c'),'-lm','-o',str(exe)],check=True)
r=subprocess.run([str(exe),*[v['file'] for v in chosen.values()]],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
(out/'adxt.txt').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
(out/'adxt.json').write_text(json.dumps({'result':'PASS','reference_source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'real_inputs':list(chosen.values()),'xsdk_audio':'mocked, real API frame layout','ISO_transport':'XbFile_* mocked with real resource bytes; separate real xb_disc test covers transport','hardware_audio':False,'PPC_execution':False,'whole_game_rollback_validated':False},indent=2)+'\n')
