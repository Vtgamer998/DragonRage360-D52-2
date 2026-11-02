#!/usr/bin/env python3
"""Regression for the actual adapted pan-blur stack-packet copy (BE crash)."""
import argparse,re,subprocess,os
from pathlib import Path
from fixed_profile_source import transform
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('output',type=Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
s,_=transform('src/sys/gfxm_a.c',(a.upstream/'src/sys/gfxm_a.c').read_text())
lines=[l.strip() for l in s.splitlines() if 'pkt.tag.w & 0xFFFFu' in l];assert len(lines)==2
code="""#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint64_t u64; typedef uint8_t u8;
int main(void){
struct {struct {uint32_t w;} tag; uint32_t pad[3]; u64 gifTag; u64 rest[9];} pkt={0};
u64 dst[12]={0},*p=dst; pkt.tag.w=0x20000005; pkt.gifTag=0x1234;
"""+'\n'.join(lines)+"""
assert(p==dst+10);assert(dst[0]==0x1234);assert(dst[10]==0);assert(dst[11]==0);
puts("PASS actual adapted pan-blur copy: tag 0x20000005 copies 80 bytes and advances 80; original BE interpretation copied 131072");return 0;}
"""
f=a.output/'packet-count.c';f.write_text(code)
subprocess.run(['gcc','-std=gnu17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(f),'-o',str(a.output/'packet-count')],check=True)
r=subprocess.run([str(a.output/'packet-count')],capture_output=True,text=True,timeout=10,env=dict(os.environ,ASAN_OPTIONS="detect_leaks=0",UBSAN_OPTIONS="halt_on_error=1"));(a.output/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
