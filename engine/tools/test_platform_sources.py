#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Exercise injected D6-style additions and verify Make refuses before CC."""
from pathlib import Path
import tempfile,shutil,json,subprocess,argparse,sys
from platform_sources import reviewed_sources,PlatformSourceError
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--d6',type=Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
project=Path(__file__).resolve().parents[2]
assert len(reviewed_sources(project))==33
cases=[]
with tempfile.TemporaryDirectory(prefix='platform-inventory-') as d:
    dest=Path(d)
    shutil.copytree(project/'engine',dest/'engine',ignore=shutil.ignore_patterns('__pycache__'))
    shutil.copyfile(project/'Makefile',dest/'Makefile')
    plat=dest/'engine/platform/platform'
    for case,path,content in [('extra C','xb_battle_activate.c','int XbBattleActivate_Fighters(void){return 0;}'),('duplicate original C','btl_obj_decomp.c','void BtlObj_UpdateAll(void){}'),('shadow header','port_compat.h','#define malloc Port_Malloc'),('opaque table','tables_dump/common2_word3.bin','not typed')]:
        f=plat/path;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(content)
        try:reviewed_sources(dest)
        except PlatformSourceError:pass
        else:raise AssertionError(case)
        for target in ('build/xb_plat_time.o','build/main.o','all'):
            run=subprocess.run(['make','-s','-j4','-C',str(dest),target,'CC=/must-not-invoke-a-compiler'],capture_output=True,text=True)
            assert run.returncode and '/must-not-invoke-a-compiler' not in run.stdout+run.stderr,(case,target,run.stdout,run.stderr)
        link=subprocess.run([sys.executable,str(dest/'engine/tools/link_battle.py'),str(dest/'no-upstream'),'/must-not-invoke-a-compiler',str(dest/'no-sdk'),str(dest/'out'),'--platform','--float-mode','native-ps2'],capture_output=True,text=True)
        assert link.returncode and 'PlatformSourceError' in link.stderr and 'FileNotFoundError' not in link.stderr,(case,link.stderr)
        f.unlink();cases.append(case)
    inventory=dest/'engine/platform/platform-sources.txt';original=inventory.read_text()
    for content in (original+'xb_plat_time.c\n',original+'../outside.c\n',original.replace('xb_plat_time.c\n','')):
        inventory.write_text(content)
        try:reviewed_sources(dest)
        except PlatformSourceError:pass
        else:raise AssertionError('malformed inventory accepted')
    inventory.write_text(original)
    f=plat/'xb_plat_time.c';f.unlink()
    try:reviewed_sources(dest)
    except PlatformSourceError:pass
    else:raise AssertionError('missing source accepted')
if a.d6:
    # Supply the new inventory to the old bundle in an isolated copy: refusal
    # must come from its actual additions, not merely its missing inventory.
    with tempfile.TemporaryDirectory(prefix='d6-source-check-') as d:
        candidate=Path(d);shutil.copytree(a.d6/'engine/platform/platform',candidate/'engine/platform/platform')
        shutil.copyfile(project/'engine/platform/platform-sources.txt',candidate/'engine/platform/platform-sources.txt')
        try:reviewed_sources(candidate)
        except PlatformSourceError as e:
            message=str(e);assert 'xb_battle_activate.c' in message and 'port_compat.h' in message and 'untyped binary resources' in message
            cases.append('D6 actual added modules/header/blobs refused with new inventory')
        else:raise AssertionError('D6 should not pass inventory audit')
report={'result':'PASS','injected_regressions_refused_before_compile':cases,'inventory_duplicates_traversal_missing_checked':True,'parallel_Make_platform_main_and_all_checked':True,'strict_link_entry_refuses_before_plugin':True,'PowerPC_execution':False}
(a.output/'platform-inventory-test.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
