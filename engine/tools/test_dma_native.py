#!/usr/bin/env python3
"""Compare adapted producers to actual pinned upstream dma.c on a LE host.
Usage: test_dma_native.py upstream out [--sanitize]. No GPU/battle execution.
"""
from pathlib import Path
import argparse,json,re,subprocess,hashlib
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('upstream',type=Path);p.add_argument('output',type=Path);p.add_argument('--sanitize',action='store_true')
a=p.parse_args();repo=a.upstream.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
engine=Path(__file__).resolve().parents[1];source=repo/'src/sys/dma.c'
assert hashlib.sha256(source.read_bytes()).hexdigest()=='b7d254aa36fd34762d217b65cf2aa4437ef62ac40d4eba13ca80451f4b8094f3','reference must be pinned upstream dma.c'
text=source.read_text();names=sorted(set(re.findall(r'\b(?:Dma_\w+|gDma\w+)\b',text)))
cc=['gcc','-std=gnu17','-O1','-g','-ffunction-sections','-fdata-sections','-fno-pie']
if a.sanitize:cc+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
reference=cc+['-fno-builtin','-Wno-pointer-to-int-cast','-Wno-int-to-pointer-cast',
             '-include',str(repo/'port/include/port_compat.h'),'-I'+str(repo/'include'),'-I'+str(repo/'port/include')]
reference+=['-D'+n+'=Ref_'+n for n in names]
r=subprocess.run(reference+['-c',str(source),'-o',str(out/'reference.o')],capture_output=True,text=True)
(out/'reference-compile.txt').write_text(r.stdout+r.stderr);r.check_returncode()
sources=[engine/'tests/test_dma_native.c',engine/'graphics/dr_dma_producer.c',engine/'graphics/dr_gs_stream.c',engine/'vu/dr_vu1.c']
cmd=cc+['-Wall','-Wextra','-Werror','-ffp-contract=off','-I'+str(engine/'graphics'),'-I'+str(engine/'vu')]
cmd+=list(map(str,sources))+[str(out/'reference.o'),'-no-pie','-Wl,--gc-sections','-lm','-o',str(out/'test-native')]
subprocess.run(cmd,check=True)
r=subprocess.run([str(out/'test-native')],capture_output=True,text=True)
(out/'native.txt').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
(out/'native.json').write_text(json.dumps({'result':'PASS','upstream_source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
    'reference':'actual separately compiled upstream src/sys/dma.c producer functions, renamed; no PS2 MMIO executed',
    'comparisons':'see native.txt','sanitizers':'ASan/UBSan' if a.sanitize else 'off','GPU_execution':False,
    'battle_executed':False,'big_endian_host_run':False,'all_engine_packet_producers_adapted':False},indent=2)+'\n')
