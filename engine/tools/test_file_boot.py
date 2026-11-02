#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
import argparse,re,subprocess,os
from pathlib import Path
from loading_error_source import transform
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('output',type=Path);a=p.parse_args();root=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
s=transform('src/sys/file.c',(a.upstream/'src/sys/file.c').read_text())
def function(name):
 m=re.search(r'^(?:void|s32) '+name+r'\([^\n]*\)\s*\{',s,re.M);assert m,name
 end=s.index('{',m.start())+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
code='\n'.join(function(n) for n in ['File_LoadPartitionNw','File_WaitPartition','File_InitAdx','File_LoadPartition1','File_Init']);src=out/'file-boot.c';src.write_text((root/'engine/tests/file_boot_ownership.c').read_text().replace('/* ORIGINAL_FUNCTIONS */',code));platform=root/'engine/platform/platform'
r=subprocess.run(['gcc','-std=gnu17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-DXB_HOST_TEST','-I'+str(a.upstream.resolve()/'include'),'-I'+str(platform),str(src),str(platform/'xb_resconv.c'),'-o',str(out/'file-boot')],capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([str(out/'file-boot')],capture_output=True,text=True,timeout=10,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'));(out/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
