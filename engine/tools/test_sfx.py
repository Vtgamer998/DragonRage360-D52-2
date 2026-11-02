#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Run the native SFX sampler against the pinned decoder and a user-owned USA ISO.
Host NT/audio transport is mocked; no hardware or whole-game execution claimed.
"""
from pathlib import Path
import argparse, subprocess, hashlib, json, os
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('xsdk',type=Path);p.add_argument('iso',type=Path);p.add_argument('output',type=Path);p.add_argument('--sanitize',action='store_true');args=p.parse_args()
e=Path(__file__).resolve().parents[1];out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
pin=subprocess.check_output(['git','-C',str(args.upstream),'rev-parse','HEAD'],text=True).strip()
assert pin=='3ca419c7966b11438e2dc3c9a7e1914f102ed545'
reference=args.upstream/'port/src/gs/snd_se.c'
s=reference.read_text();a=s.index('static void decode_block(');b=s.index('/* SDL',a)
fn=s[a:b].replace('nib << 12','nib * 4096')
code='''#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#define SPU_TOTAL 0x200000
static uint8_t sSpu[SPU_TOTAL];
typedef struct {int on,bank,id,handle;uint32_t pos,loopPos;int h1,h2;int16_t buf[28];int have;double frac,step;int last,ended;int16_t prev;float gl,gr,fade;int stopping;} Voice;
'''+fn+'''
static Voice voice;
void Ref_Spu(const uint8_t *p,size_t n){memset(sSpu,0,sizeof(sSpu));memcpy(sSpu+0x5010,p,n);}
void Ref_Start(uint32_t pos,uint32_t rate,int pitch,int vol,int pan){memset(&voice,0,sizeof(voice));voice.pos=pos+0x5010;voice.step=rate*pow(2.,pitch/1200.)/48000.;double t=pan/127.*1.5707963267948966;voice.gl=(float)(cos(t)*1.41421356)*(vol/127.f)*.25f/32768.f;voice.gr=(float)(sin(t)*1.41421356)*(vol/127.f)*.25f/32768.f;}
void Ref_Mix(float *out){memset(out,0,1536*4);for(unsigned i=0;i<256;i++){while(voice.frac>=voice.have){voice.frac-=voice.have;decode_block(&voice);if(voice.ended)break;}if(voice.ended)break;int ip=(int)voice.frac;float a=ip?voice.buf[ip-1]:voice.prev,b=voice.buf[ip];float v=a+(b-a)*(float)(voice.frac-ip);out[i]=v*voice.gl;out[256+i]=v*voice.gr;voice.frac+=voice.step;}}
'''
(out/'reference.c').write_text(code)
cmd=['gcc','-D_GNU_SOURCE','-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-I'+str(e/'platform/platform'),'-I'+str(e.parent/'src'),'-idirafter',str(args.xsdk.resolve()/'include')]
if args.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
exe=out/'test-sfx';sources=[e/'tests/test_xb_sfx.c',*[e/'platform/platform'/('xb_'+n+'.c') for n in ['sfx','adxt','iop','disc','plat_file','iso_file']],e.parent/'src/dr_disc.c',out/'reference.c']
subprocess.run(cmd+list(map(str,sources))+['-lm','-o',str(exe)],check=True)
r=subprocess.run([str(exe),str(args.iso.resolve())],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'sfx.txt').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
(out/'sfx.json').write_text(json.dumps({'result':'PASS','upstream':pin,'reference_sha256':hashlib.sha256(reference.read_bytes()).hexdigest(),'real_bank':'AFS1 index 3194, pzs3us1/03194.bin','compared_stereo_frames':6144,'pitches_cents':[-1200,0,1200],'real_ISO_transport':'xb_disc, dr_disc, xb_iso_file, xb_plat_file; NT file system mocked','sample_transfer':'actual xb_iop + xb_sfx; low-address host heap fixture','ADXT_mix':'actual decoder + sampler, single mocked SDK client','hardware_audio':False,'PPC_execution':False,'complete_SPU_emulation':False},indent=2)+'\n')

