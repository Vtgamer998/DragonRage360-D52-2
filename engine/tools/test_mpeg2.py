#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Test actual mounted ISO/PSS CPU decoder; ffmpeg comparison is host-only."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,time
import numpy as np
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('xsdk',type=Path);p.add_argument('iso',type=Path);p.add_argument('output',type=Path)
p.add_argument('--sanitize',action='store_true');p.add_argument('--full',action='store_true')
a=p.parse_args();engine=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
platform=engine/'platform/platform'
base=['gcc','-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-I'+str(platform),'-I'+str(engine.parent/'src'),'-I'+str(a.xsdk.resolve()/'include')]
if a.sanitize:base+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
def run(cmd,log):
    start=time.monotonic();r=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,env=env)
    (out/log).write_text(r.stdout+r.stderr);r.check_returncode();return time.monotonic()-start
exe=out/'test-mpeg2'
run(base+[engine/'tests/test_xb_mpeg2.c',platform/'xb_mpeg2.c',platform/'xb_movie_source.c',platform/'xb_iso_file.c',platform/'xb_disc.c',engine.parent/'src/dr_disc.c','-o',exe],'compile.txt')
cases=[]
for name,path,total in [('opening','disc/DATA/ZS3USOP.PSS',2810),('ending','disc/DATA/ZS3USED.PSS',4200)]:
    pss=out/(name+'.pss');rgba=out/(name+'.rgba');ref=out/(name+'-ffmpeg.rgba')
    frames=total if a.full else 32
    cmd=[exe,a.iso.resolve(),path,pss,rgba,frames]
    if a.full:cmd+=['--expect-eof']
    elapsed=run(cmd,name+'.txt')
    run(['ffmpeg','-v','error','-i',pss,'-frames:v',32,'-vf','scale=in_range=tv:out_range=pc:flags=neighbor','-pix_fmt','rgba','-f','rawvideo','-y',ref],name+'-reference.txt')
    native=np.frombuffer(rgba.read_bytes(),dtype=np.uint8).reshape(32,448,512,4)
    reference=np.frombuffer(ref.read_bytes(),dtype=np.uint8).reshape(native.shape)
    assert np.all(native[:,:,:,3]==255) and np.count_nonzero(native[:,:,:,:3])>10000
    diff=np.abs(native[:,:,:,:3].astype(np.int16)-reference[:,:,:,:3].astype(np.int16))
    mean=float(diff.mean());maximum=int(diff.max())
    assert mean<2 and maximum<=8,(name,mean,maximum)
    digest=hashlib.sha256()
    with pss.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):digest.update(block)
    cases.append({'name':name,'path':path,'pss_bytes':pss.stat().st_size,'pss_sha256':digest.hexdigest(),'decoded_frames':frames,'EOF_checked':a.full,'ffmpeg_compared_frames':32,'RGB_mean_absolute_error':mean,'RGB_maximum_error':maximum,'host_sanitized_seconds':elapsed,'rgba_sha256':hashlib.sha256(rgba.read_bytes()).hexdigest()})
core=out/'test-mpeg2-core'
run(base+[engine/'tests/test_xb_mpeg2_core.c',platform/'xb_mpeg2.c','-Wl,--wrap=calloc,--wrap=realloc','-o',core],'compile-core.txt')
run([core,out/'opening.pss'],'negative.txt')
report={'result':'PASS','sanitizers':a.sanitize,'cases':cases,'negative_tests':['invalid PES header','IO failure','short output capacity','sticky error','unchanged output on failure','dimension mismatch','unsupported P-picture','all byte truncations through first picture header','allocation failures (decoder, ES, planes)','ES cache cap 2 MiB','reader close ownership','mount/unmount/remount invalidation including buffered bytes'],
        'source_sha256':{n:hashlib.sha256((platform/n).read_bytes()).hexdigest() for n in ['xb_mpeg2.c','xb_movie_source.c','xb_mpeg2.h']},'transport':'actual xb_disc/dr_disc and mounted ISO; only NT transport mocked','ffmpeg_version':subprocess.check_output(['ffmpeg','-version'],text=True).splitlines()[0],
        'PPC_execution':False,'Xbox_GPU_pixels':False,'Sony_sceMpeg_wrapper_integrated':False,'audio_video_sync':False,'fight_running':False}
(out/'mpeg2.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2),flush=True)
