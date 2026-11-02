#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Real bank conversion plus original CPU metadata consumers; no audio device."""
from pathlib import Path
import argparse,re,subprocess,os
from fixed_profile_source import transform
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('output',type=Path);p.add_argument('banks',nargs='+',type=Path);a=p.parse_args();assert len(a.banks) in (4,5,7);root=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
text,_=transform('src/sys/snd.c',(a.upstream/'src/sys/snd.c').read_text())
def function(name):
 m=re.search(r'^[\w *]+\b'+name+r'\([^\n]*\)\s*\{',text,re.M)
 if not m:raise ValueError(name)
 at=text.index('{',m.start());end=at+1;level=1
 while level:level+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[m.start():end]+'\n'
code='\n'.join(function(n) for n in ['SndHd_GetVagInfo','SndHd_GetVagMax','SndHd_GetLoopFlags','SndHd_GetLoopBits','Snd_SetupBank'])
src=out/'sound_bank_transport.c';src.write_text((root/'engine/tests/test_sound_bank_transport.c').read_text().replace('/* ORIGINAL_FUNCTIONS */',code));platform=root/'engine/platform/platform';exe=out/'sound-bank-transport'
cmd=['gcc','-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-DXB_HOST_TEST','-I'+str(platform),'-I'+str(a.upstream.resolve()/'include'),str(src),str(platform/'xb_sound_bank.c'),str(platform/'xb_resconv.c'),'-o',str(exe)]
r=subprocess.run(cmd,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([str(exe),*map(lambda p:str(p.resolve()),a.banks)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'),timeout=20);(out/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
