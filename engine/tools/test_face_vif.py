#!/usr/bin/env python3
"""Build/run the bounded face reader tests; emits no commercial resource bytes.
Usage: test_face_vif.py game.iso output-directory [--sanitize]
"""
import hashlib,json,os,pathlib,re,subprocess,sys
engine=pathlib.Path(__file__).resolve().parents[1]
iso=pathlib.Path(sys.argv[1]).resolve();out=pathlib.Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
sanitize='--sanitize' in sys.argv
flags=['-std=c17','-O1' if sanitize else '-O2','-g','-Wall','-Wextra','-Werror','-ffp-contract=off',
       '-I'+str(engine/'resource'),'-I'+str(engine/'vu')]
if sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-sanitize-recover=all']
unit_sources=[engine/'resource/dr_face_vif.c',engine/'tests/test_face_vif.c']
corpus_sources=[engine/'tests/face_vif_real.c',engine/'vu/dr_vu1.c',*[engine/'resource'/n for n in
                ['dr_disc.c','dr_container.c','dr_bpe.c','dr_anim.c','dr_skeleton.c','dr_face_vif.c']]]
logs=[]
for name,sources,args in [('face-unit',unit_sources,[]),('face-real',corpus_sources,[str(iso)])]:
    exe=out/name
    subprocess.run(['gcc',*flags,*map(str,sources),'-lm','-o',str(exe)],check=True)
    result=subprocess.run([str(exe),*args],check=True,capture_output=True,text=True)
    logs.append(result.stdout);print(result.stdout,end='')
    if result.stderr:print(result.stderr,file=sys.stderr,end='')
stats={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',logs[1])}
report={'result':'PASS','classification':'face geometry DMA/VIF lists; previously misclassified as texture payloads',
        'corpus':stats,'unit':'bounds, preflight, guard/padding, truncation, NUM=0, unknown opcodes, callbacks, 10000 hostile buffers',
        'VU_test':'every UNPACK is isolated after reset with equal CL/WL and zero base/TOPS; transferred bytes compared exactly',
        'MSCNT_execution':False,'game_engine_linked':False,'GPU_execution':False,'Xbox_execution':False,
        'new_XEX':False,'sanitizers':['ASan','UBSan'] if sanitize else [],
        'leak_detection':'disabled' if 'detect_leaks=0' in os.environ.get('ASAN_OPTIONS','') else 'runtime default',
        'input_bytes':iso.stat().st_size,
        'source_sha256':{str(p.relative_to(engine)):hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in dict.fromkeys(unit_sources+corpus_sources+[engine/'tests/real_iso_sanitize.c'])}}
(out/'face-vif-results.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
