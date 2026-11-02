#!/usr/bin/env python3
"""Extract bounded test fixtures from an external USA disc; never package them."""
from pathlib import Path
import urllib.request,struct,json,concurrent.futures,argparse,time
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--iso',type=Path);p.add_argument('--url');a=p.parse_args();assert bool(a.iso)!=bool(a.url)
r=a.output.resolve();r.mkdir(parents=True,exist_ok=True);total=3020816384
u=lambda b,at:struct.unpack_from('<I',b,at)[0]
def read(at,n):
 if a.iso:
  with a.iso.open('rb') as f:f.seek(at);data=f.read(n)
 else:
  for retry in range(3):
   req=urllib.request.Request(a.url+('?d14='+str(at)+'-'+str(retry)),headers={'Range':f'bytes={at}-{at+n-1}'})
   try:
    with urllib.request.urlopen(req,timeout=60) as res:
     assert res.status==206 and res.headers.get('Content-Range')==f'bytes {at}-{at+n-1}/{total}'
     data=res.read(n+1)
    assert len(data)==n,(at,n,len(data))
    break
   except Exception:
    if retry==2:raise
 assert len(data)==n;return data
first=r/'iso-first8m.bin'
if not first.exists():first.write_bytes(read(0,8*1024*1024))
b=first.read_bytes();files={}
def scan(at,n,prefix='',depth=0):
 assert depth<8 and at+n<=len(b);i=0
 while i<n:
  size=b[at+i]
  if not size:i=(i//2048+1)*2048;continue
  rec=b[at+i:at+i+size];i+=size;name=rec[33:33+rec[32]]
  if name in (b'\0',b'\1'):continue
  name=name.decode().split(';')[0];off=u(rec,2)*2048;length=u(rec,10)
  if rec[25]&2:scan(off,length,prefix+name+'/',depth+1)
  else:files[prefix+name]=(off,length)
rr=b[32768+156:32768+190];scan(u(rr,2)*2048,u(rr,10));(r/'iso-files.json').write_text(json.dumps(files,indent=2))
parts={};spans={}
for part in range(3):
 at,n=next(v for k,v in files.items() if k.endswith(f'PZS3US{part}.AFS'));head=read(at,8);assert head[:4]==b'AFS\0';table=read(at+8,u(head,4)*8);parts[part]=(at,head+table)
 for i in range(u(head,4)):
  off,n=struct.unpack_from('<II',table,i*8);spans[part,i]=(at+off,n)
rows=[]
for name,id in [('left-effects',0x599),('right-effects',0x5a3),('common-2',2),('common-3',3),('common-4',4),('left-model',0x590),('right-model',0x59a),('left-animation',0x598),('right-animation',0x5a2),('sound-common',0x14a),('sound-stage',0x14e),('voice-left',0xbda),('voice-right',0xbdb),('stage',0x171),('member-6',6),('member-7',7),('member-8',8),('member-11',11)]:rows.append((1,id-1,r/'resources'/(name+'.bin')))
rows += [(1,0x170+i,r/'stages'/f'stage-{i:02}.bin') for i in range(39)]
rows += [(2,65116+i,r/'resources'/f'ambience-{i:02}.bin') for i in range(39)]
rows += [(2,64974,r/'resources'/'battle-bgm.bin')]
for name in ['SLUS_216.78','BIN/DBZP.BIN']:
 at,n=files[name];dest=r/'programs'/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(b[at:at+n] if at+n<=len(b) else read(at,n))
fixture=r/'stage-fixture.iso'
with fixture.open('wb') as f:
 f.truncate(total);f.write(b)
 for at,data in parts.values():f.seek(at);f.write(data)
def fetch(row):
 part,id,dest=row;at,n=spans[part,id];assert 0<=n<20*1024*1024;dest.parent.mkdir(parents=True,exist_ok=True)
 if not dest.exists() or dest.stat().st_size!=n:dest.write_bytes(read(at,n) if n else b'')
 return at,dest
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 for at,dest in pool.map(fetch,rows):
  with fixture.open('r+b') as f:f.seek(at);f.write(dest.read_bytes())
  print(dest.name,dest.stat().st_size,flush=True)
print('External test fixtures complete; exclude this output directory from deliverables.',flush=True)
