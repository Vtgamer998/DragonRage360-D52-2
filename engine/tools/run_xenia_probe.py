#!/usr/bin/env python3
"""Run one scoped native probe under Xenia + Xvfb in the same namespace."""
import argparse,os,pathlib,shutil,subprocess,time,re,json,signal
p=argparse.ArgumentParser();p.add_argument('xex',type=pathlib.Path);p.add_argument('iso',type=pathlib.Path);p.add_argument('out',type=pathlib.Path);p.add_argument('--deps',type=pathlib.Path,required=True);p.add_argument('--token',default='original-fighter');p.add_argument('--seconds',type=int,default=240);p.add_argument('--display',type=int,default=173);p.add_argument('--threads',type=int,choices=[1,2,4,8],default=4);p.add_argument('--audio-driver',choices=['disk','dummy'],default='disk',help='host SDL output only; dummy makes no audio/FPS claim');a=p.parse_args()
out=a.out.resolve();out.mkdir(parents=True,exist_ok=True);(out/'isos').mkdir(exist_ok=True)
shutil.copy2(a.xex,out/'default.xex');dst=out/'isos'/a.iso.name
if not dst.exists():os.link(a.iso.resolve(),dst)
(out/'startup_test.txt').write_text(a.token+'\n');deps=a.deps.resolve();host=deps/'host-root';env=dict(os.environ,LD_LIBRARY_PATH=str(host/'usr/lib/x86_64-linux-gnu'),DISPLAY=f'127.0.0.1:{a.display}',VK_ICD_FILENAMES=str(host/'usr/share/vulkan/icd.d/lvp_icd.json'),XDG_RUNTIME_DIR=str(out),LP_NUM_THREADS=str(a.threads),SDL_AUDIODRIVER=a.audio_driver,SDL_DISKAUDIOFILE=str(out/'audio.raw'))
old_log=out/'storage/cache/default.log'
if old_log.exists():old_log.unlink()
xf=(out/'xvfb.log').open('w');ef=(out/'process.log').open('w')
xv=subprocess.Popen([str(host/'usr/bin/Xvfb'),f':{a.display}','-screen','0','1280x768x24','-ac','-extension','GLX','-listen','tcp','-nolisten','unix','-nolisten','local','-xkbdir',str(host/'usr/share/X11/xkb')],env=env,stdout=xf,stderr=xf)
xe=None;captured=set();final_taken=False;action_capture=False;began=time.monotonic();result={}
try:
 time.sleep(2)
 xe=subprocess.Popen([str(deps/'xenia/build/bin/Linux/Release/xenia_canary'),str(out/'default.xex'),'--gpu=vulkan','--readback_resolve=full','--apu=sdl','--log_file='+str(out/'xenia.log'),'--storage_root='+str(out/'storage')],cwd=out,env=env,stdout=ef,stderr=ef)
 log=out/'storage/cache/default.log'
 while time.monotonic()-began<a.seconds and xe.poll() is None:
  time.sleep(.25);s=log.read_text(errors='replace') if log.exists() else ''
  hits=re.findall(r'Battle_StepFrame frame=(\d+) returned=(-?\d+)',s)
  frame=int(hits[-1][0]) if hits else -1
  if frame>=0 and frame not in captured:
   subprocess.run(['import','-window','root',str(out/f'frame-{frame}.png')],env=env,timeout=15,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);captured.add(frame)
  if 'action response tick=51' in s and not action_capture:
   subprocess.run(['import','-window','root',str(out/'after-action51.png')],env=env,timeout=15,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);action_capture=True
  if 'frame times samples=' in s and not final_taken:
   subprocess.run(['import','-window','root',str(out/'frame-final.png')],env=env,timeout=15,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);final_taken=True
  if 'typed-data diagnostic returned to UI loop' in s or 'frame failure has' in s:break
 subprocess.run(['import','-window','root',str(out/'last-window.png')],env=env,timeout=15,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 result={'elapsed_seconds':time.monotonic()-began,'frames_seen':sorted(captured),'process_exit':xe.poll(),'token':a.token,'renderer':'llvmpipe; no physical-console performance claim','llvmpipe_threads':a.threads,'host_audio_output':a.audio_driver,'audible_audio_verified':False}
 print(json.dumps(result,indent=2),flush=True)
finally:
 for child in [xe,xv]:
  if child and child.poll() is None:
   child.terminate()
   try:child.wait(timeout=8)
   except subprocess.TimeoutExpired:child.kill();child.wait()
 (out/'harness.json').write_text(json.dumps(result,indent=2)+'\n');xf.close();ef.close()
