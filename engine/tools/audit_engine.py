#!/usr/bin/env python3
"""Compile audit only: objects are NOT a linked or executable game."""
import concurrent.futures,json,pathlib,subprocess,sys
repo=pathlib.Path(sys.argv[1]).resolve();cc=str(pathlib.Path(sys.argv[2]).resolve());out=pathlib.Path(sys.argv[3]).resolve();out.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(repo/'port/tools'))
import portsrc,eeconst
flags=['-std=gnu89','-O2','-w','-fno-strict-aliasing','-ffp-contract=off','-fcommon','-fno-builtin','-msoft-float','-include','port_libm.h','-Iinclude','-Iport/include','-include','port_compat.h']
def build(f):
 src,enabled,missing=portsrc.prepare(f);name=str(f.relative_to(repo)).replace('/','_')[:-2];obj=out/(name+'.o');pre=out/(name+'.i')
 cmd=[cc,*flags,'-I'+str(f.parent)]
 p=subprocess.run(cmd+['-E',str(src)],cwd=repo,capture_output=True,text=True)
 if not p.returncode:
  pre.write_text(eeconst.transform(p.stdout))
  p=subprocess.run(cmd+['-c','-fpreprocessed',str(pre),'-o',str(obj)],cwd=repo,capture_output=True,text=True)
 error='\n'.join(p.stderr.splitlines()[:12])
 pre.unlink(missing_ok=True)
 return {'source':str(f.relative_to(repo)),'compiled':p.returncode==0,'missing_c':missing,'error':error}
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
 rows=list(pool.map(build,portsrc.sources()))
report={'upstream':'3ca419c7966b11438e2dc3c9a7e1914f102ed545','target':'powerpc-xsdk-elf','total':len(rows),'compiled':sum(x['compiled'] for x in rows),'files':rows,'warning':'Compile-only audit; no engine link, no data conversion, no renderer or networking integration.'}
(out/'audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='files'}),flush=True)
for r in rows:
 if not r['compiled']:print(r['source'],r['error'][:500])
