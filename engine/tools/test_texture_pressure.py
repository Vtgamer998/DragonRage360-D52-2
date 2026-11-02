#!/usr/bin/env python3
"""Run the actual frontend pressure sweep with bounded texture-lifetime mocks."""
from pathlib import Path
import argparse,subprocess,json,os
p=argparse.ArgumentParser();p.add_argument('out',type=Path);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True);r=Path(__file__).resolve().parents[2]
s=(r/'engine/capture/port/src/gs/gs_draw.c').read_text();at=s.index('        /* full (by count, or by size:');start=s.rfind('    if (',0,at);end=s.index('    tex_t0 = gpu_now();',at);block=s[start:end]
source=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define TEX_CAP 8
#define TEX_BYTES_CAP 1000u
#define TEX_BUCKETS 32u
typedef struct {unsigned tex,last,bytes;uint64_t tex0,texa;unsigned gen;} Tex;
static Tex sTex[8],*sLast;static uint16_t sTexBucket[TEX_BUCKETS];
static unsigned gsTexCount,gGsFrame,gGsTexturePressureSweeps,gGsTexturePressureFallbacks,sTexBytes;
static int slots;static unsigned destroyed;static const int gsWhite=99;
static int left(void){return slots;}
static void destroy(unsigned tex){assert(tex>0 && tex<=8);destroyed|=1u<<tex;slots++;}
static struct {int(*texSlotsLeft)(void);void(*texDestroy)(unsigned);} api={left,destroy},*sBackend=&api;
static unsigned tex_bucket(uint64_t a,uint64_t b,unsigned c){return (unsigned)(a+b+c)%TEX_BUCKETS;}
static int sweep(void){unsigned i;
'''+block+r'''
 return -1;}
static void setup(unsigned n,int free_slots,unsigned bytes){
 memset(sTex,0,sizeof(sTex));memset(sTexBucket,0,sizeof(sTexBucket));gGsFrame=200;gsTexCount=n;sTexBytes=n*bytes;slots=free_slots;destroyed=gGsTexturePressureSweeps=gGsTexturePressureFallbacks=0;
 for(unsigned i=0;i<n;i++){sTex[i]=(Tex){.tex=i+1,.last=200,.bytes=bytes,.tex0=i};}
 sLast=&sTex[0];
}
int main(void){
 setup(3,0,100);sTex[0].last=120;assert(sweep()==-1 && slots==1 && gsTexCount==2 && destroyed==2 && gGsTexturePressureSweeps==1 && !gGsTexturePressureFallbacks && !sLast);assert(sTex[0].tex==2 && sTex[1].tex==3);
 setup(3,0,100);assert(sweep()==gsWhite && slots==0 && gsTexCount==3 && !destroyed && gGsTexturePressureFallbacks==1);
 setup(8,1,100);sTex[0].last=0;assert(sweep()==-1 && slots==2 && gsTexCount==7 && destroyed==2);
 setup(3,1,400);sTex[0].last=120;assert(sweep()==-1 && sTexBytes==800 && gsTexCount==2 && destroyed==2);
 setup(3,1,100);assert(sweep()==-1 && !destroyed && sLast==sTex && !gGsTexturePressureSweeps);
 setup(0,0,100);assert(sweep()==gsWhite && !destroyed && gGsTexturePressureFallbacks==1);
 puts("PASS 6 actual texture-pressure sweeps; stale textures released before white fallback, current-frame textures retained, count/byte budgets and unreclaimable pressure bounded");
}
'''
f=a.out/'fixture.c';f.write_text(source);exe=a.out/'check';subprocess.run(['gcc','-std=gnu17','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie',str(f),'-o',str(exe)],check=True)
x=subprocess.run([str(exe)],text=True,capture_output=True,check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'));print(x.stdout,end='');(a.out/'test.log').write_text(x.stdout+x.stderr);(a.out/'result.json').write_text(json.dumps({'result':'PASS','cases':6,'actual_frontend_sweep':True,'current_frame_textures_preserved':True,'scope':'host lifetime mocks; native pixels separately tested'},indent=2)+'\n')
