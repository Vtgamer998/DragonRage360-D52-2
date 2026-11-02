#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check that the old stripper cannot emit an empty release from this build."""
from pathlib import Path
import argparse,json,shutil,struct,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__);p.add_argument('native_elf',type=Path);p.add_argument('native_map',type=Path);p.add_argument('prefix');p.add_argument('output',type=Path)
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);e=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='dr-data-refusal-') as tmp:
 root=Path(tmp);elf=root/'native.elf';shutil.copy2(a.native_elf,elf);shutil.copy2(a.native_map,str(elf)+'.map')
 data=root/'gamedata/disc';(data/'BIN').mkdir(parents=True)
 source=bytearray(0x200);source[:7]=b'\x7fELF\x01\x01\x01';struct.pack_into('<I',source,0x20,0x100);struct.pack_into('<HH',source,0x2e,40,2)
 struct.pack_into('<6I',source,0x128,0,1,2,0x100000,0x40,64);source[0x40:0x80]=bytes(range(64))
 (data/'SLUS_216.78').write_bytes(source);(data/'BIN/DBZP.BIN').write_bytes(bytes(64))
 release=root/'release'
 cmd=['python3',str(e/'platform/tools/xb_strip_data.py'),'--exe',str(elf),'--prefix',a.prefix,'--data',str(root/'gamedata'),'--out',str(release)]
 r=subprocess.run(cmd,capture_output=True,text=True);(out/'strip-refusal.txt').write_text(r.stdout+r.stderr)
 assert r.returncode and 'No complete data layout found' in r.stderr,(r.returncode,r.stdout,r.stderr)
 assert not release.exists(),'incomplete release must not be written'
 (out/'strip-refusal.json').write_text(json.dumps({'result':'PASS','real_native_ELF_inspected':True,'sources':'synthetic files only','release_emitted':False},indent=2)+'\n')
 print('PASS: actual native ELF/map/binutils inspected; incomplete skeleton layout refused, no release/dat emitted')
