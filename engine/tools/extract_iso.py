#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Extract required BT3 files with the actual native ISO reader; no game execution.
User-provided ISO and extracted game files are local inputs, not redistributable
port artifacts. Default extracts the two programs, SYSTEM.CNF and three AFSs.
"""
from pathlib import Path
import argparse,ctypes as C,hashlib,json,struct,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__);p.add_argument('iso',type=Path);p.add_argument('output',type=Path)
p.add_argument('--programs-only',action='store_true',help='extract only SYSTEM.CNF and the two programs; never copy the AFS archives')
p.add_argument('--inspect-only',action='store_true');p.add_argument('--cc',default='gcc');a=p.parse_args()
project=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
class Span(C.Structure):_fields_=[('offset',C.c_uint64),('size',C.c_uint64),('directory',C.c_uint)]
class Disc(C.Structure):_fields_=[('read',C.c_void_p),('opaque',C.c_void_p),('size',C.c_uint64),('root',Span)]
Reader=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint64,C.c_void_p,C.c_size_t)
with tempfile.TemporaryDirectory(prefix='dr-iso-reader-') as tmp,a.iso.open('rb') as file:
 library=Path(tmp)/'disc.so'
 subprocess.run([a.cc,'-std=c17','-O2','-Wall','-Wextra','-Werror','-fPIC','-shared','-I'+str(project/'src'),str(project/'src/dr_disc.c'),'-o',str(library)],check=True)
 lib=C.CDLL(str(library));lib.dr_mount.argtypes=[C.POINTER(Disc),Reader,C.c_void_p,C.c_uint64];lib.dr_find.argtypes=[C.POINTER(Disc),C.c_char_p,C.POINTER(Span)]
 lib.dr_read.argtypes=[C.POINTER(Disc),C.POINTER(Span),C.c_uint64,C.c_void_p,C.c_size_t]
 lib.dr_error.argtypes=[C.c_int];lib.dr_error.restype=C.c_char_p
 @Reader
 def read(opaque,at,target,n):
  try:
   file.seek(at);data=file.read(n)
   if len(data)!=n:return -1
   C.memmove(target,data,n);return 0
  except (OSError,ValueError):return -1
 def checked(rc):
  if rc:raise RuntimeError(lib.dr_error(rc).decode())
 disc=Disc();checked(lib.dr_mount(C.byref(disc),read,None,a.iso.stat().st_size))
 paths=['SYSTEM.CNF','SLUS_216.78','BIN/DBZP.BIN']+([] if a.programs_only else [f'DATA/PZS3US{i}.AFS' for i in range(3)])
 spans=[];report={'ISO_bytes':a.iso.stat().st_size,'reader':'actual src/dr_disc.c','game_executed':False,'files':[]}
 for name in paths:
  span=Span();checked(lib.dr_find(C.byref(disc),name.encode(),C.byref(span)));assert not span.directory
  row={'name':name,'offset':span.offset,'size':span.size}
  if name.endswith('.AFS'):
   head=C.create_string_buffer(8);checked(lib.dr_read(C.byref(disc),C.byref(span),0,head,8))
   assert head.raw[:4]==b'AFS\0',name
   count=struct.unpack_from('<I',head.raw,4)[0];assert count<=1000000 and 8+count*8<=span.size
   table=C.create_string_buffer(count*8);checked(lib.dr_read(C.byref(disc),C.byref(span),8,table,count*8))
   for i,(offset,size) in enumerate(struct.iter_unpack('<II',table.raw)):
    assert offset<=span.size and size<=span.size-offset and (not size or offset>=8+count*8),(name,i)
   row['AFS_entries']=count;row['all_entry_ranges_valid']=True
  spans.append((span,row))
 # All required paths/ranges are checked before extracting any file.
 for span,row in spans:
  if not a.inspect_only:
   dest=out/'disc'/row['name'];dest.parent.mkdir(parents=True,exist_ok=True)
   buffer=C.create_string_buffer(8*1024*1024);digest=hashlib.sha256();at=0
   with dest.open('wb') as target:
    while at<span.size:
     n=min(len(buffer),span.size-at);checked(lib.dr_read(C.byref(disc),C.byref(span),at,buffer,n))
     chunk=buffer.raw[:n];target.write(chunk);digest.update(chunk);at+=n
   row['sha256']=digest.hexdigest()
  report['files'].append(row);print(json.dumps(row),flush=True)
 (out/'iso-extraction.json').write_text(json.dumps(report,indent=2)+'\n')
 print('PASS: selected ISO files readable; no game or Xbox execution',flush=True)
