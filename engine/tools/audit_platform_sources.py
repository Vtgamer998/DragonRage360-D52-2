#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
import argparse,json
from pathlib import Path
from platform_sources import reviewed_sources,PlatformSourceError
p=argparse.ArgumentParser(description='Refuse unreviewed platform additions before compiling; no gameplay readiness claim.')
p.add_argument('project',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
try:
    sources=reviewed_sources(a.project)
    report={'result':'PASS','reviewed_platform_source_count':len(sources),'sources':[str(f.relative_to(a.project.resolve())) for f in sources],'gameplay_ready':False}
except PlatformSourceError as e:
    report={'result':'REFUSED','error':str(e),'gameplay_ready':False}
if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2));raise SystemExit(0 if report['result']=='PASS' else 1)
