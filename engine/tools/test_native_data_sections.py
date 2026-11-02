#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Validate strict closure and fail-closed legacy isolation in a real PPC build."""
import argparse, json, re, subprocess, struct
from pathlib import Path
import native_data_sections
from build_overlay_catalog import read_object

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('build',type=Path);p.add_argument('output',type=Path)
a=p.parse_args();build=a.build.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
r=json.loads((build/'battle-integration.json').read_text())
assert r['link']=='PASS' and not r['unresolved_count'] and not r['ABI_conflict_count']
args=json.loads((build/'link-arguments.json').read_text());cc=Path(args[0])
elf=build/'battle-link.elf';b,h,ss,names,syms=read_object(elf)
defined={s[0]:s for s in syms if s[0] and s[4]}
assert 'Battle_Main' in defined and 'Game_Main' not in defined
assert not [s[0] for s in syms if s[0] and not s[4] and s[3]>>4!=2]
ctype=defined['_ctype_'];sec=ss[ctype[4]]
table=b[sec[4]+ctype[1]-sec[3]:sec[4]+ctype[1]-sec[3]+ctype[2]]
assert len(table)==257 and table[ord('A')+1]&1 and table[ord('a')+1]&2 and table[ord('0')+1]&4
assert not any(s in defined for s in ['D_002D3FF8','D_002FAA08','__xb_ps2_ctype_table'])

# Restore explicit roots for BOTH legacy groups. The original 103 undefined
# targets must reappear: no --unresolved-symbols=ignore / weak stubs / zeros.
negative=[x.replace(str(build/'battle-link'),str(out/'legacy-root')) for x in args]
negative.insert(1,'-Wl,--undefined=D_002D3FF8,--undefined=D_002FAA08')
run=subprocess.run(negative,capture_output=True,text=True)
(out/'legacy-root.txt').write_text(run.stdout+run.stderr)
unresolved=sorted(set(re.findall(r"undefined reference to [`']([^`']+)'",run.stderr)))
assert run.returncode and len(unresolved)==103

# A newly introduced native data reference to a legacy table must be rejected
# by the section audit *before* the normal link can silently retain it.
source=out/'incoming.s';source.write_text('.section .data.probe,"aw",@progbits\n.globl native_bad_reference\nnative_bad_reference:\n.long D_002D3FF8\n')
obj=out/'incoming.o'
subprocess.run([str(cc),'-mcpu=cell','-c',str(source),'-o',str(obj)],check=True)
check=out/'incoming';(check/'prepared').mkdir(parents=True,exist_ok=True)
rows=r['files']+[{'source':'test/native_bad_reference.s','object':str(obj),'compiled':True}]
try:
    native_data_sections.audit(rows,cc,check)
except ValueError as e:
    assert 'native reference' in str(e)
else:
    raise AssertionError('incoming legacy reference accepted')
audit=json.loads((check/'native-data-sections.json').read_text())
assert len(audit['incoming_native_relocations'])==1

# The splitter refuses changed upstream layouts instead of guessing a cut.
for row in r['files']:
    if row['source'] in native_data_sections.RULES:
        try: native_data_sections.prepare(row['source'],Path(row['original_source']).read_text()+'\n')
        except ValueError: pass
        else: raise AssertionError('unreviewed upstream accepted')

# Probe the wider original menu boot too; it is NOT made a claimed game boot.
probe=[x.replace(str(build/'battle-link'),str(out/'game-main-probe')) for x in args]
probe.insert(1,'-Wl,--undefined=Game_Main')
run=subprocess.run(probe,capture_output=True,text=True)
(out/'game-main-probe.txt').write_text(run.stdout+run.stderr)
boot=sorted(set(re.findall(r"undefined reference to [`']([^`']+)'",run.stderr)))
assert run.returncode and boot
report={'result':'PASS','Battle_Main_linked':True,'Game_Main_linked':False,
        'strong_undefined':0,'SDK_ctype_verified':True,
        'all_original_data_and_relocations_preserved':True,
        'explicit_legacy_roots_still_fail':True,'legacy_unimplemented_symbols':unresolved,
        'new_native_legacy_reference_refused':True,'upstream_layout_change_refused':True,
        'original_Game_Main_probe_missing_count':len(boot),
        'original_Game_Main_probe_missing':boot,
        'PPC_execution':False,'game_data_initialized':False,'battle_executed':False}
(out/'native-data-tests.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: Battle_Main PPC link; 103 legacy symbols preserved/reappear when rooted; native legacy reference and changed skeleton rejected; real SDK ctype; original Game_Main probe still misses',len(boot),'names; no execution')
