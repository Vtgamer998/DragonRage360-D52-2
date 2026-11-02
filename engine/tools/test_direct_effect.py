#!/usr/bin/env python3
"""Actual adapted EftMesh_SendTri uses the bounded serializer, not a raw mixed-layout copy."""
from pathlib import Path
import argparse,re,subprocess,json,os
from fixed_profile_source import transform
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('prefix');p.add_argument('out',type=Path);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True);r=Path(__file__).resolve().parents[2]
s,_=transform('src/battle/eft_ad_b.c',(a.upstream/'src/battle/eft_ad_b.c').read_text());at=s.index('void EftMesh_SendTri(',s.index('/* Sends one textured gouraud triangle'));end=s.index('\n}',at)+2
names=['EftAdXyzf','EftAdRgbaq','EftAdSt','EftMeshTexTriPkt'];types='\n'.join(re.search(r'typedef struct '+n+r' \{.*?\} '+n+r';',s,re.S)[0] for n in names)
source=r'''
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "dr_ot_native.h"
typedef uint8_t u8;typedef uint16_t u16;typedef int8_t s8;typedef uint32_t u32;typedef int32_t s32;typedef uint64_t u64;typedef int64_t s64;typedef float f32;
typedef struct {s32 x,y,z;} EftAdScr;typedef struct {f32 x,y,z,w;} EftAdVec;
'''+types+r'''
static uint8_t arena[112] __attribute__((aligned(16)));static int begun,ended,rejected,counted,fail_range;
static u8 *Dma_BeginDirect(void){begun++;return arena;}
static void Dma_EndDirect(void *p){assert((u8 *)p==arena+112);ended++;}
static int dr_dma_w64_range(void *p,unsigned long n){assert(p==arena && n==112);return fail_range;}
static int dr_dma_reject_packet(void){rejected++;return -420;}
static void XbFreshBoot_CountDirectEffect(void){counted++;}
'''+s[at:end]+r'''
static uint64_t get(const void *p){uint64_t v;memcpy(&v,p,8);return v;}
int main(void){
 EftAdScr p[3]={{100,200,0x123456},{300,400,0x234567},{500,600,0x345678}};
 EftAdVec c[3]={{10,20,30,64},{40,50,60,80},{70,80,90,96}},t[3]={{.25f,.75f,1,0},{.5f,.25f,2,0},{.75f,1,.5f,0}};
 EftMesh_SendTri(&p[0],&p[1],&p[2],&c[0],&c[1],&c[2],&t[0],&t[1],&t[2],0x12345678,1);
 assert(begun==1 && ended==1 && !rejected && counted==1);assert(get(arena)==0xC400000000008001ull && get(arena+8)==0xF42142142160ull);
 for(unsigned i=0;i<3;i++){
  unsigned off=32+i*24;uint32_t q;memcpy(&q,&t[i].z,4);uint64_t rgba=(uint8_t)c[i].x|(uint64_t)(uint8_t)c[i].y<<8|(uint64_t)(uint8_t)c[i].z<<16|(uint64_t)(uint8_t)c[i].w<<24|(uint64_t)q<<32;
  assert(get(arena+off)==rgba);assert(get(arena+off+16)==((uint64_t)p[i].x|(uint64_t)p[i].y<<16|(uint64_t)p[i].z<<32|0xffull<<56));
 }
 memset(arena,0xa5,sizeof(arena));fail_range=1;EftMesh_SendTri(&p[0],&p[1],&p[2],&c[0],&c[1],&c[2],&t[0],&t[1],&t[2],0,0);assert(rejected==1 && ended==1 && counted==1);for(unsigned i=0;i<112;i++)assert(arena[i]==0xa5);
 for(unsigned i=0;i<3;i++){c[i].w=0;}
 EftMesh_SendTri(&p[0],&p[1],&p[2],&c[0],&c[1],&c[2],&t[0],&t[1],&t[2],0,0);assert(begun==2);
 puts("PASS actual immediate effect triangle serializer: colors/Q/XYZF, full payload, transparent early exit, range rejection without mutation; BE conversion separately covered by actual PPC/LE fixture");
}
'''
f=a.out/'fixture.c';f.write_text(source);exe=a.out/'check';subprocess.run(['gcc','-std=gnu17','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+str(r/'src'),str(f),str(r/'src/dr_ot_native.c'),'-o',str(exe)],check=True)
x=subprocess.run([str(exe)],text=True,capture_output=True,check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'));print(x.stdout,end='');(a.out/'test.log').write_text(x.stdout+x.stderr)
subprocess.run([a.prefix+'gcc','-mcpu=cell','-std=gnu17','-O2','-Wall','-Wextra','-Werror','-I'+str(r/'src'),'-c',str(f),'-o',str(a.out/'native.o')],check=True)
(a.out/'result.json').write_text(json.dumps({'result':'PASS','actual_adapted_function':'EftMesh_SendTri','host_serialization_and_rejection':True,'PPC_fixture_compiled':True,'BE_struct_conversion':'separate actual PPC/LE OT fixture','specific_user_effect_visually_verified':False},indent=2)+'\n')
