#!/usr/bin/env python3
"""Exercise actual palette lookup with deliberate hash collisions and failed allocations."""
from pathlib import Path
import argparse, json, os, subprocess
p=argparse.ArgumentParser();p.add_argument('out',type=Path);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
r=Path(__file__).resolve().parents[2]
s=(r/'engine/capture/port/src/gs/gs_draw.c').read_text()
body=s[s.index('static GsTex clut_texture('):s.index('/* ((A - B)',s.index('static GsTex clut_texture('))]
decl=s[s.index('static struct { uint32_t hash, last; GsTex tex; } sCluts'):s.index('static uint32_t tex_bucket')]
if 'sClutPx' not in decl:  # allow the unchanged D46 function as a negative control
    decl+='static uint32_t sClutPx[MAX_CLUTS][256];\n'
fixture=r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define MAX_CLUTS 2
#define GS_TEXFMT_RGBA8 0
typedef unsigned GsTex;
static unsigned gGsFrame, reads, uploads, creates, drops, rejected, next_id;
static int slots, fail_alloc, fail_create;
static uint32_t pixels[256];
static unsigned char live[512];
static uint32_t Gs_VramRead(uint32_t cbp,unsigned bw,unsigned psm,unsigned x,unsigned y){
 (void)cbp;assert(bw==1 && psm==0);unsigned n=x+16*y;
 unsigned i=(n&0xe7)|((n&8)<<1)|((n&16)>>1);reads++;return pixels[i];
}
static int left(void){return slots;}
static void drop(GsTex t){assert(t<512 && live[t]);live[t]=0;slots++;drops++;}
static GsTex create(unsigned w,unsigned h,unsigned fmt,unsigned levels){
 assert(w==256 && h==1 && fmt==0 && levels==1);creates++;
 if(fail_create)return 0;
 assert(slots>0);slots--;assert(++next_id<512);live[next_id]=1;return next_id;
}
static void upload(GsTex t,unsigned level,unsigned w,unsigned h,unsigned fmt,void *data,unsigned bytes){
 assert(live[t] && !level && w==256 && h==1 && !fmt && bytes==1024);
 const unsigned char *b=data;
 for(unsigned i=0;i<256;i++)for(unsigned ch=0;ch<4;ch++)assert(b[4*i+ch]==(unsigned char)(pixels[i]>>(8*ch)));
 uploads++;free(data);
}
static void dr_gs_backend_reject(void){rejected++;}
static void *allocate(size_t n){return fail_alloc?NULL:malloc(n);}
static struct {int(*texSlotsLeft)(void);void(*texDestroy)(GsTex);GsTex(*texCreate)(unsigned,unsigned,unsigned,unsigned);void(*texUpload)(GsTex,unsigned,unsigned,unsigned,unsigned,void*,unsigned);} api={left,drop,create,upload},*sBackend=&api;
#define malloc allocate
'''+decl+body+r'''
#undef malloc
static uint32_t hash(const uint32_t *p,int fold){uint32_t h=2166136261u;for(unsigned i=0;i<256;i++)h=(h^p[i]^(fold?p[i]>>16:0))*16777619u;return h;}
static void collide(uint32_t *b,const uint32_t *a,int fold){
 memcpy(b,a,1024);uint32_t h=2166136261u;for(unsigned i=0;i<254;i++)h=(h^b[i]^(fold?b[i]>>16:0))*16777619u;
 b[254]^=1;h=(h^b[254]^(fold?b[254]>>16:0))*16777619u;
 uint32_t x=h^(hash(a,fold)*899433627u);b[255]=fold?x^(x>>16):x;
 assert(memcmp(a,b,1024) && hash(a,fold)==hash(b,fold));
}
static void reset(void){memset(sCluts,0,sizeof(sCluts));memset(sClutPx,0,sizeof(sClutPx));memset(live,0,sizeof(live));gGsFrame=10;slots=8;next_id=reads=uploads=creates=drops=rejected=0;fail_alloc=fail_create=0;for(unsigned i=0;i<256;i++)pixels[i]=0x80402010u+i*0x01030107u;}
int main(void){uint32_t a[256],b[256];GsTex first,second;
 reset();first=clut_texture(0);assert(first && reads==256 && uploads==1);gGsFrame++;assert(clut_texture(0)==first && uploads==1 && !drops);
 /* Old hash collisions must not alias; both distinct palettes remain usable. */
 reset();memcpy(a,pixels,1024);collide(b,a,0);first=clut_texture(0);memcpy(pixels,b,1024);second=clut_texture(0);assert(second && first!=second && uploads==2);memcpy(pixels,a,1024);assert(clut_texture(0)==first && uploads==2);
 /* A deliberate collision even in the folded new hash proves exact equality is necessary. */
 reset();memcpy(a,pixels,1024);collide(b,a,1);first=clut_texture(0);memcpy(pixels,b,1024);second=clut_texture(0);assert(first!=second && second && uploads==2);
 /* Neither palette referenced by this frame may be evicted for a third. */
 pixels[0]^=123;assert(!clut_texture(0) && !drops && uploads==2);
 gGsFrame++;assert(clut_texture(0) && drops==1 && uploads==3);
 reset();slots=0;assert(!clut_texture(0) && !creates && !drops);
 /* An evicted handle must not survive allocation/creation failure and be returned later. */
 for(int mode=0;mode<2;mode++){
  reset();memcpy(a,pixels,1024);first=clut_texture(0);pixels[0]^=1;second=clut_texture(0);assert(second!=first);
  gGsFrame++;pixels[0]^=2;if(mode)fail_create=1;else fail_alloc=1;
  assert(!clut_texture(0) && drops==1 && !live[first]);
  fail_alloc=fail_create=0;memcpy(pixels,a,1024);GsTex fresh=clut_texture(0);assert(fresh && fresh!=first && live[fresh]);
 }
 puts("PASS: exact CLUT identity, old/new hash collisions, reuse, index order, RGBA upload bytes, current-frame lifetime, slot exhaustion, allocation and texture failure recovery");
}
'''
f=a.out/'fixture.c';f.write_text(fixture);exe=a.out/'clut-check'
subprocess.run(['gcc','-std=gnu17','-O2','-Wall','-Wextra','-Werror','-DDR_XBOX360','-fsanitize=address,undefined','-fno-pie','-no-pie',str(f),'-o',str(exe)],check=True)
v=subprocess.run([str(exe)],text=True,capture_output=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'));(a.out/'test.log').write_text(v.stdout+v.stderr);v.check_returncode();print(v.stdout,end='')
(a.out/'result.json').write_text(json.dumps({'result':'PASS','actual_clut_function':True,'forced_old_and_new_hash_collisions':True,'sanitizers':['ASAN','UBSAN'],'native_byte_upload_checked':True,'GPU_execution':False,'XEX_built':False,'FPS_measured':False},indent=2)+'\n')
