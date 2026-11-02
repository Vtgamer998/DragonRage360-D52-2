#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Exercise original initial sound job only. Never runs Battle_Load or models."""
from pathlib import Path
import argparse,re,subprocess,os
from fixed_profile_source import transform
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('output',type=Path);p.add_argument('banks',nargs=4,type=Path);a=p.parse_args();up=a.upstream.resolve();root=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
def functions(path,names,adapt=True):
 text=path.read_text()
 if adapt:text,_=transform(path.relative_to(up).as_posix(),text)
 result=[]
 for name in names:
  m=re.search(r'^[\w *]+\b'+name+r'\([^\n]*\)\s*\{',text,re.M)
  if not m:raise ValueError(name)
  end=text.index('{',m.start())+1;level=1
  while level:level+=(text[end]=='{')-(text[end]=='}');end+=1
  result.append(text[m.start():end]+'\n')
 return '\n'.join(result)
code=functions(up/'src/sys/file.c',['File_GetPendingCount','File_InitRequests','File_UpdateRequests','XbFile_RequestBounded'])
code+=functions(up/'src/sys/job.c',['Job_Clear','Job_Init','Job_Destroy','Job_Run','Job_Push','Job_GetCount'])
code+=functions(up/'src/sys/snd.c',['SndHd_GetVagInfo','SndHd_GetVagMax','SndHd_GetLoopFlags','SndHd_GetLoopBits','Snd_SetupBank'])
code+=functions(up/'src/battle/battle_load.c',['BtlJob_GetPool','BtlJob_InitPool','BtlJob_Alloc','BtlJob_Free','BtlLoad_StepInitial','BtlLoad_PushInitialJob'])
src=out/'initial_audio_job.c';src.write_text((root/'engine/tests/test_initial_audio_job.c').read_text().replace('/* ORIGINAL_FUNCTIONS */',code));platform=root/'engine/platform/platform';exe=out/'initial-audio-job'
flags=['-std=gnu17','-O1','-g','-fno-builtin','-ffunction-sections','-fdata-sections','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(up/'include'),'-I'+str(platform)]
objs=[]
for name in ['list','queue']:
 obj=out/(name+'.o');r=subprocess.run(['gcc',*flags,'-c',str(up/'src/sys'/f'{name}.c'),'-o',str(obj)],capture_output=True,text=True);(out/(name+'-compile.log')).write_text(r.stdout+r.stderr);r.check_returncode();objs.append(obj)
cmd=['gcc',*flags,'-Wall','-Wextra','-Werror','-DXB_HOST_TEST',str(src),str(platform/'xb_sound_bank.c'),str(platform/'xb_resconv.c'),*map(str,objs),'-Wl,--gc-sections','-o',str(exe)]
r=subprocess.run(cmd,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([str(exe),*map(lambda p:str(p.resolve()),a.banks)],capture_output=True,text=True,timeout=20);(out/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
