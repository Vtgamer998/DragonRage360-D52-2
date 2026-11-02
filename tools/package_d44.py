#!/usr/bin/env python3
"""Package verified D44 sources and both diagnostic XEX variants, without game data."""
from pathlib import Path
import argparse,hashlib,json,zipfile

ROOT=Path(__file__).resolve().parents[1]
DENIED={'.xex','.o','.elf','.pyc','.so','.iso','.raw','.bin','.zip','.tmp','.raw'}
HISTORICAL={'README-D42.md','RESULTADO-D42.json','SHA256SUMS-D42.txt'}

def source_files():
    for p in sorted(ROOT.rglob('*')):
        if not p.is_file():continue
        rel=p.relative_to(ROOT);name=rel.as_posix()
        if any(x in rel.parts for x in ['__pycache__','build','.git','upstream']):continue
        if p.suffix.lower() in DENIED or p.name.startswith('private-'):continue
        if rel.parts[0]=='xenia':continue
        if name.startswith('validation-original/prepared/'):continue
        if rel.parts[0]=='validation-adaptation' and name not in ['validation-adaptation/result.json','validation-adaptation/publisher.log']:continue
        if rel.parts[0].startswith('validation-d'):
            if len(rel.parts)<3 or rel.parts[1] not in ['final','native','xenia','reproduction','toolchain','resource-audit']:continue
            if 'prepared' in rel.parts or p.suffix not in ['.json','.txt','.log','.png','.gz']:continue
        if name=='SHA256SUMS-ADAPTACAO.txt':continue
        if name in HISTORICAL or rel.parts[0]=='evidencias-d42':name='historico-D42/'+name
        yield p,name

def build(output,compiled):
    files=list(source_files())
    if compiled:
        for variant,folder in [('console',ROOT),('xenia',ROOT/'xenia')]:
            meta=json.loads((folder/'static.json').read_text())
            assert meta['source_delivery']=='D44'
            assert hashlib.sha256((folder/'default.xex').read_bytes()).hexdigest()==meta['sha256']
            assert meta['model_shader_bridge_linked'] and meta['command_budget_live_cursor_linked'] and meta['GPU_state_cache_linked']
            files.append((folder/'default.xex','default.xex' if variant=='console' else 'xenia/default.xex'))
            if variant=='xenia':
                for name in ['static.json','engine-diagnostic.json','startup_test.txt']:
                    files.append((folder/name,'xenia/'+name))
    names=[n for _,n in files];assert len(names)==len(set(names))
    manifest=''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+n+'\n' for p,n in files)
    temp=output.with_name(output.name+'.tmp');output.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(temp,'w',zipfile.ZIP_DEFLATED,6) as z:
        for p,name in files:z.write(p,'DragonRage360/'+name)
        z.writestr('DragonRage360/SHA256SUMS-ADAPTACAO.txt',manifest)
    with zipfile.ZipFile(temp) as z:
        assert z.testzip() is None
        assert not any(n.lower().endswith(('.iso','.raw','.bin','.so','.o','.elf','.pyc')) for n in z.namelist())
        xex=[n for n in z.namelist() if n.endswith('.xex')]
        assert len(xex)==(2 if compiled else 0)
        proof=json.loads(z.read('DragonRage360/RESULTADO-D44.json'))
        assert proof['new_XEX_built'] and not proof['complete_game'] and proof['NOT_READY']==2
        assert proof['runtime_actions']['result'] in ['PASS','INCOMPLETE']
        assert proof['full_GPU_runtime']['result'] in ['PASS','INCOMPLETE']
        for row in json.loads(z.read('DragonRage360/vendor/tenkaichi-original/IMPORT-MANIFEST.json'))['files']:
            b=z.read('DragonRage360/vendor/tenkaichi-original/'+row['path'])
            assert hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest()==row['git_blob_sha']
        for p,n in files:assert hashlib.sha256(z.read('DragonRage360/'+n)).digest()==hashlib.sha256(p.read_bytes()).digest()
    temp.replace(output)
    return {'file':str(output),'bytes':output.stat().st_size,'files':len(files)+1,'sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'XEX_count':len(xex)}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=ROOT.parent);a=p.parse_args();out=a.output.resolve()
    results=[build(out/'DragonRage360-adaptacao-upstream-arquivo.zip',False),build(out/'DragonRage360-D44-pacote.zip',True)]
    print(json.dumps({'result':'PASS','original_source_hashes':'PASS','commercial_game_data_included':False,'packages':results},indent=2))

if __name__=='__main__':main()
