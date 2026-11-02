#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Exercise recovered RGB24 producer and actual transformed allocation bodies."""
from pathlib import Path
import argparse, os, subprocess, sys
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('output',type=Path)
a=p.parse_args();root=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(a.upstream/'port/tools'))
import portsrc
from fixed_profile_source import transform
text,_,_=portsrc.enable_attempts((a.upstream/'src/battle/stg_b.c').read_text())
text,_=transform('src/battle/stg_b.c',text)
(out/'stg_b.transformed.c').write_text(text)
assert 'Dma_AddData(&gScrXfade->chain[i], 0x10)' not in text
def body(name):
    start=text.index('void '+name+'(');opening=text.index('{',start);depth=1;i=opening+1
    while depth:
        depth+=(text[i]=='{')-(text[i]=='}');i+=1
    return text[start:i]
test=r'''
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
typedef struct {void *buf[2];} ScrXfade;
static ScrXfade state,*gScrXfade=&state;
static int attempt,fail_at,live,error;
static void XbRes_SetReadError(int e){error=e;}
static void *Heap_Alloc(int n,int a,int b,int c){(void)a;(void)b;(void)c;
 assert(!state.buf[0] && !state.buf[1]);if(++attempt==fail_at)return NULL;
 void *p=malloc(n);assert(p);live++;return p;}
static void Heap_Free(void *p){assert(p);free(p);live--;}
'''+body('ScrXfade_AllocBuffers')+'\n'+body('ScrXfade_FreeBuffers')+r'''
int main(void){for(int f=1;f<=3;f++){
 attempt=live=error=0;fail_at=f;ScrXfade_AllocBuffers();
 if(f<3){assert(error==-423 && !state.buf[0] && !state.buf[1] && !live);}
 else {assert(!error && live==2);ScrXfade_AllocBuffers();assert(attempt==2);}
 ScrXfade_FreeBuffers();ScrXfade_FreeBuffers();assert(!live);
 }
 state.buf[1]=malloc(16);live=1;ScrXfade_FreeBuffers();assert(!live);
 gScrXfade=NULL;ScrXfade_AllocBuffers();assert(error==-423);ScrXfade_FreeBuffers();
 puts("PASS: transformed original cross-fade allocation: both failures, atomic publication, idempotent and partial cleanup");}
'''
alloc=out/'allocation.c';alloc.write_text(test)
flags=['gcc','-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
units=[('allocation',[alloc]),('rgb24',[root/'engine/tests/test_dma_xfade.c',root/'src/dr_dma_producer.c',root/'src/dr_gs_stream.c',root/'src/dr_vu1.c'])]
for name,files in units:
    exe=out/name
    subprocess.run([*flags,'-I'+str(root/'src'),*map(str,files),'-lm','-o',str(exe)],check=True)
    r=subprocess.run([str(exe)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'));(out/(name+'.log')).write_text(r.stdout+r.stderr)
    print(r.stdout+r.stderr,end='');r.check_returncode()
