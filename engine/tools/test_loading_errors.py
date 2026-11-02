#!/usr/bin/env python3
"""Execute adapted ORIGINAL queue/loading/battle control functions with fake devices.
This tests failure control flow on host; it is not an original battle boot.
"""
import argparse,subprocess
from pathlib import Path
from loading_error_source import transform
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[2];up=a.upstream.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
def function(text,name):
 import re
 m=re.search(r'^(?:s32|void|int)\s+\*?'+name+r'\([^\n]*\)\s*\{',text,re.M)
 if not m:raise ValueError(name)
 start=m.start();at=text.index('{',m.start());level=1;end=at+1
 while level:
  level+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]+'\n'
file=transform('src/sys/file.c',(up/'src/sys/file.c').read_text())
loading=transform('src/sys/loading.c',(up/'src/sys/loading.c').read_text())
work=transform('src/battle/battle_work.c',(up/'src/battle/battle_work.c').read_text())
battle=(root/'engine/combat/battle.c').read_text()
cases=[('queue',file,['File_GetSize','File_GetPendingCount','File_InitRequests','File_UpdateRequests','XbFile_RequestBounded','File_Request','XbFile_RequestOwned','File_LoadSyncEx','XbFile_AbortRequestsRetainBuffers','File_CancelRequests']),('loading',loading,['Load_RunBlocking']),('battle',work,['Battle_Load'])]
for name,source,functions in cases:
 fixture=(root/'engine/tests'/('loading_error_'+name+'.c')).read_text()
 code='\n'.join(function(source,f) for f in functions)
 if name=='battle':code+='\n'+function(battle,'Battle_Init')+'\n'+function(battle,'Battle_Main')
 src=out/(name+'.c');src.write_text(fixture.replace('/* ORIGINAL_FUNCTIONS */',code))
 cmd=['gcc','-std=gnu99','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=undefined','-fno-sanitize-recover=all','-ffunction-sections','-fdata-sections','-I'+str(root/'engine/platform/platform'),'-I'+str(up/'include'),str(src),str(root/'engine/platform/platform/xb_resconv.c'),'-DXB_HOST_TEST','-Wl,--gc-sections','-o',str(out/name)]
 if name=='queue':
  obj=out/'list.o'
  subprocess.run(['gcc','-std=gnu99','-O1','-g','-fsanitize=undefined','-ffunction-sections','-fdata-sections','-I'+str(up/'include'),'-c',str(up/'src/sys/list.c'),'-o',str(obj)],check=True)
  cmd.insert(-2,str(obj))
 r=subprocess.run(cmd,capture_output=True,text=True);(out/(name+'-compile.log')).write_text(r.stdout+r.stderr);r.check_returncode()
 r=subprocess.run([str(out/name)],capture_output=True,text=True,timeout=10);(out/(name+'-test.log')).write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
print('PASS: original control flow with simulated IO, loading presentation and subsystem devices; no PPC/GPU/game execution')
