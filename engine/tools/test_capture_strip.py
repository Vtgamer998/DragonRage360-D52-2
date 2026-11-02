#!/usr/bin/env python3
"""Actual native VU callback contracts: successful explicit culls versus incomplete strips."""
from pathlib import Path
import subprocess,argparse,json,os
p=argparse.ArgumentParser();p.add_argument('out',type=Path);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
r=Path(__file__).resolve().parents[2];text=(r/'src/dr_capture_demo.c').read_text()
a0=text.index('static int stage_shader(');b=text.index('static void le64',a0);callbacks=text[a0:b]
source=r'''
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#define DR_VU_INVALID -200
static unsigned gsVuVertCount,gsVuIdxCount,gsSkipped;
static struct {unsigned culled_strips;} capture_profile;
static struct {uint64_t frame[2],tex0[2],prim;} gGs;
static int mode,backend_error,rejected;
static int GsGpu_Enabled(void){return mode!=8;}
static void Gs_Gif(const void *p,unsigned n){assert(p && n==2);}
static void Gs_RegWrite(unsigned r,uint64_t v){assert(!r);gGs.prim=v;}
static int dr_gs_backend_error(void *p){(void)p;return backend_error;}
static void dr_gs_backend_reject(void){rejected++;}
static void xsdk_log_write(const char *p,...){(void)p;}
static void emit(unsigned n){
 if(mode==0){gsVuVertCount+=n;gsVuIdxCount+=(n-2)*3;}
 if(mode==1)gsSkipped++;
 if(mode==3){gsSkipped++;gsVuVertCount++;}
 if(mode==4){gsSkipped++;gsVuIdxCount++;}
 if(mode==5)gsSkipped+=2;
 if(mode==6){gsVuVertCount+=n;gsVuIdxCount+=(n-2)*3-1;}
 if(mode==7){gsSkipped++;backend_error=-101;}
}
static void GsGpu_DrawVu4(int c,const float *v,unsigned n,const float *u){(void)c;(void)v;(void)u;emit(n);}
static void GsGpu_DrawVu0(int l,int c,const float *v,unsigned n,const float *u){(void)l;GsGpu_DrawVu4(c,v,n,u);}
'''+callbacks+r'''
int main(void){unsigned cases=0;uint8_t env[32]={0};float v[36]={0},c[108]={0};
 for(unsigned shader=0;shader<2;shader++)for(mode=0;mode<9;mode++){
  gsVuVertCount=10;gsVuIdxCount=20;gsSkipped=30;backend_error=0;rejected=0;capture_profile.culled_strips=0;
  int e=shader?model_shader(NULL,env,4,v,3,c,0):stage_shader(NULL,env,4,v,3,c);
  assert((e==0)==(mode<=1));assert(capture_profile.culled_strips==(unsigned)(mode==1));
  assert(rejected==(mode>1 && mode<8));cases++;
 }
 printf("PASS %u actual callback cases: explicit cull accepted, silent/partial/duplicate skips, capacity/backend failures refused\n",cases);
}
'''
f=a.out/'fixture.c';f.write_text(source);exe=a.out/'fixture';cmd=['gcc','-std=gnu17','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie',str(f),'-o',str(exe)]
subprocess.run(cmd,check=True);run=subprocess.run([str(exe)],text=True,capture_output=True,check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'));print(run.stdout,end='');(a.out/'result.json').write_text(json.dumps({'result':'PASS','actual_callback_cases':18,'scope':'callback contracts with frontend mocks; actual prolonged battle logged separately'},indent=2)+'\n')
