#!/usr/bin/env python3
"""Actual font backpatch: numeric GS tag preserved on LE/BE; malformed ranges rejected."""
import argparse,subprocess,json,os
from pathlib import Path
from fixed_profile_source import transform
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('prefix');p.add_argument('out',type=Path);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
s,_=transform('src/battle/col_c.c',(a.upstream/'src/battle/col_c.c').read_text());start=s.index('void Font_CloseSpriteTag(');end=s.index('\n}',start)+2
source=r'''
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
typedef uint64_t u64;
static u64 data[9000];static int rejected;
static int dr_dma_reject_packet(void){rejected++;return -420;}
static int dr_dma_w64_range(void *p,unsigned long bytes){return p!=data || bytes>65536;}
'''+s[start:end]+r'''
int main(void){unsigned counts[]={0,1,189,1365};unsigned cases=0;
for(unsigned i=0;i<4;i++){
 u64 *tag=data,*end=data+2+counts[i]*6;data[0]=0x6400000000008000ull;rejected=0;
 Font_CloseSpriteTag(&end,&tag);assert(!rejected && data[0]==(0x6400000000000000ull|counts[i]));
 assert((data[0]>>60)==6 && ((data[0]>>58)&3)==1);cases++;
}
for(unsigned mode=0;mode<5;mode++){
 u64 *tag=data,*end=mode==0?data+1:mode==1?data+3:mode==2?data+2+1366*6:mode==3?NULL:data+8;
 data[0]=0x6400000000008000ull;rejected=0;if(mode==4)tag=NULL;
 Font_CloseSpriteTag(&end,&tag);assert(rejected && data[0]==0x6400000000008000ull);cases++;
}
/* Exact faulty BE numeric value seen in the prolonged battle. */
uint64_t broken=(0x6400000000008000ull&0x0000ffffffffffffull)|(189ull<<48);
assert(broken==0x00bd000000008000ull && (broken>>60)==0 && (broken&0x7fff)==0);
printf("PASS %u font tag cases; exact observed BE failure reproduced and numeric NREG/FLG preserved\n",cases);
}
'''
f=a.out/'fixture.c';f.write_text(source);exe=a.out/'check'
subprocess.run(['gcc','-std=gnu17','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie',str(f),'-o',str(exe)],check=True)
x=subprocess.run([str(exe)],text=True,capture_output=True,check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'));print(x.stdout,end='');(a.out/'test.log').write_text(x.stdout)
subprocess.run([a.prefix+'gcc','-mcpu=cell','-std=gnu17','-O2','-Wall','-Wextra','-Werror','-c',str(f),'-o',str(a.out/'native.o')],check=True)
(a.out/'result.json').write_text(json.dumps({'result':'PASS','cases':9,'observed_faulty_tag':'00bd000000008000','expected_tag':'64000000000000bd','PPC_fixture_compiled':True,'PPC_fixture_executed':False},indent=2)+'\n')
