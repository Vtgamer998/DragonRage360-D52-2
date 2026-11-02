#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
from pathlib import Path
import argparse,json,copy,subprocess
from reviewed_unused import audit,with_link_objects
p=argparse.ArgumentParser();p.add_argument('manifest',type=Path);p.add_argument('build',type=Path);p.add_argument('compiler',type=Path);p.add_argument('output',type=Path);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);m=json.loads(a.manifest.read_text());report=json.loads((a.build/'battle-integration.json').read_text());rows=with_link_objects(report['files'],a.build);excluded,proof=audit(m['regions'],rows);assert sum(x['length'] for x in excluded)==46
src=out/'live-pool.c';src.write_text('extern char D_002FEB88[];void *new_native_consumer=D_002FEB88;\n');obj=out/'live-pool.o';subprocess.run([str(a.compiler.resolve()),'-mcpu=cell','-c',str(src),'-o',str(obj)],check=True)
try:audit(m['regions'],rows+[{'source':'injected.c','compiled':True,'object':str(obj)}])
except ValueError:pass
else:raise AssertionError('new pool consumer was hidden')
bad=copy.deepcopy(rows);next(r for r in bad if r['source']=='src/battle/bobj_a.c')['source_sha256']='changed'
try:audit(m['regions'],bad)
except ValueError:pass
else:raise AssertionError('changed vector reader accepted')
(out/'unused-test.json').write_text(json.dumps({'result':'PASS','excluded_original_only_bytes':46,'new_native_consumer_rejected':True,'changed_vector_reader_rejected':True,'gaps_are_not_guessed_types':True},indent=2)+'\n');print('PASS: original-only pool proof; injected allocated native reference and changed vector source rejected')
