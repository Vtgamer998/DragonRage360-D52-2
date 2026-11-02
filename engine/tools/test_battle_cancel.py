#!/usr/bin/env python3
from pathlib import Path
import argparse,subprocess,os
p=argparse.ArgumentParser();p.add_argument('output',type=Path);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);e=Path(__file__).resolve().parents[1];s=(e/'platform/platform/xb_fresh_boot.c').read_text();start=s.index('static int cancel_requested(');end=s.index('{',start)+1;depth=1
while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
f=out/'check.c';f.write_text('''#include "xb_pad.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static int cancel_test,fail,connected;static unsigned buttons;
static int poll(int i,XbPadState*p){assert(!i);memset(p,0,sizeof(*p));p->connected=connected;p->buttons=buttons;return fail;}
int(*gXbPadPoll)(int,XbPadState*);
void xsdk_log_write(const char *s,...){(void)s;}
'''+s[start:end]+'''
int main(void){assert(!cancel_requested(0));gXbPadPoll=poll;connected=1;for(unsigned i=0;i<65536;i++){buttons=i;assert(cancel_requested(0)==((i&0x30)==0x30));}connected=0;assert(!cancel_requested(0));connected=1;buttons=0x30;fail=1;assert(!cancel_requested(0));fail=0;buttons=0;cancel_test=1;assert(!cancel_requested(3)&&cancel_requested(4));puts("PASS actual between-frame cancel: all 65536 button masks, disconnected and poll failure, bounded automated cancel");}
''');exe=out/'check';subprocess.run(['gcc','-std=gnu17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+str(e/'platform/platform'),str(f),'-o',str(exe)],check=True);r=subprocess.run([str(exe)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'));(out/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
