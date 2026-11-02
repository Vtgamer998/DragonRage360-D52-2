#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Reproduce the D6 comparison without extracting or executing its code/assets."""
from pathlib import Path,PurePosixPath
import argparse,zipfile,hashlib,json
p=argparse.ArgumentParser();p.add_argument('baseline',type=Path);p.add_argument('d6',type=Path);p.add_argument('correction',type=Path);p.add_argument('upstream',type=Path);p.add_argument('output',type=Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
def digest(b):return hashlib.sha256(b).hexdigest()
def files(path):
    result={}
    with zipfile.ZipFile(path) as z:
        for item in z.infolist():
            if item.is_dir():continue
            n=item.filename.removeprefix('DragonRage360-Demo/')
            if n in result or PurePosixPath(n).is_absolute() or '..' in PurePosixPath(n).parts:raise ValueError('invalid archive entry')
            result[n]=z.read(item)
    return result
base,d6,fix=map(files,(a.baseline,a.d6,a.correction))
added=sorted(d6.keys()-base.keys());removed=sorted(base.keys()-d6.keys());changed=sorted(n for n in base.keys()&d6.keys() if base[n]!=d6[n])
assert changed==['src/main.c'] and not removed
assert fix['src/main.c']==base['src/main.c']
assert d6['default.xex']==base['default.xex']
headers=[n for n in added if n.startswith('engine/include/battle/')]
assert all(d6[n]==(a.upstream/n.removeprefix('engine/')).read_bytes() for n in headers)
copies={'engine/platform/platform/btl_obj_decomp.c':'src/battle/btl_obj.c','engine/platform/platform/btl_char_mgr_decomp.c':'src/battle/btl_char_mgr.c','engine/platform/platform/port_compat.h':'port/include/port_compat.h'}
assert all(d6[n]==(a.upstream/old).read_bytes() for n,old in copies.items())
blobs=[{'path':n,'bytes':len(d6[n]),'sha256':digest(d6[n])} for n in added if n.endswith('.bin')]
# These are source facts, not a general-purpose semantic C analyzer.
activate=d6['engine/platform/platform/xb_battle_activate.c'].decode()
boot=d6['engine/platform/platform/xb_boot_combat.c'].decode()
tables=d6['engine/platform/platform/xb_tables_complete.c'].decode()
assert 'XbBattleActivate_CompleteTables' in activate and 'For now return success to unblock boot' in activate
assert 'extern int gScenarioReady' in boot and 'gScenarioReady = 1' in boot
assert 'if (common2) free(common2)' in tables and 'gBtlChars->tbl[0] = FILE_TABLE(common2, 3)' in tables
assert 'if (common4) free(common4)' in tables and 'gBtlObjWork->res.unk0 = (s32)(uintptr_t)common4' in tables
main=d6['src/main.c'].decode();assert main.index('XbBattleActivate_Fighters();')<main.index('xsdk_gpu_open();')
report={'result':'PASS','meaning':'comparison assertions passed; D6 is NOT a playable or validated upgrade',
        'input_archives':[{'name':f.name,'sha256':digest(f.read_bytes())} for f in (a.baseline,a.d6,a.correction)],
        'added_files':added,'changed_files':changed,'removed_files':removed,
        'correction_main_exactly_matches_D5':True,'XEX_identical_to_D5':True,'XEX_sha256':digest(base['default.xex']),
        'upstream_identical_headers':len(headers),'upstream_identical_modules_and_shim':copies,
        'untyped_binary_additions':blobs,'untyped_binary_total_bytes':sum(b['bytes'] for b in blobs),
        'findings':['CompleteTables exists but returns success without completing tables; correction readme is imprecise about its absence',
                    'boot writes a separately declared gScenarioReady, not the actual gPortDataStripped/NOT_READY state; Battle_Main call is commented out',
                    'common2/common4 pointers are published then their buffers are freed',
                    'raw tail buffers have no field conversion or native destination layout validation',
                    'facial/render wrappers call APIs not implemented by the baseline',
                    'main invokes activation before the diagnostic has prepared its resources'],
        'hardware_execution':False,'assets_redistributed':False}
(a.output/'D6-comparison.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k not in ('added_files','untyped_binary_additions','input_archives')},indent=2))
