#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Compile the ORIGINAL battle top-level and compare its direct deps to the title.
Usage: audit_battle_link.py <upstream> <powerpc-xsdk-elf-gcc> <title.elf> <out>
This is a dependency audit, NOT a battle link, endian audit or execution test.
"""
from pathlib import Path
import hashlib,json,subprocess,sys
repo,cc,title,out=(Path(p).resolve() for p in sys.argv[1:5]);out.mkdir(parents=True,exist_ok=True)
source=repo/'src/battle/battle.c';nm=cc.with_name(cc.name[:-3]+'nm');obj=out/'battle-top-level.o'
cmd=[str(cc),'-mcpu=cell','-fno-pic','-std=gnu17','-ffunction-sections','-fdata-sections','-O2','-Wall','-Wextra','-Werror','-include',str(repo/'port/include/port_compat.h'),'-I'+str(repo/'include'),'-I'+str(repo/'port/include'),'-c',str(source),'-o',str(obj)]
r=subprocess.run(cmd,capture_output=True,text=True);(out/'battle-compile.txt').write_text(r.stdout+r.stderr);r.check_returncode()
undef=subprocess.check_output([str(nm),'-u',str(obj)],text=True)
defined=subprocess.check_output([str(nm),'--defined-only',str(title)],text=True)
missing=sorted(set(line.split()[-1] for line in undef.splitlines())-set(line.split()[-1] for line in defined.splitlines()))
(out/'battle-direct-undefined.txt').write_text(undef)
report={'top_level_compilation':'PASS','source':'upstream src/battle/battle.c','source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'scope':'direct undefined symbols of this one object compared with the current title ELF; NOT a complete transitive engine dependency count','title_sha256':hashlib.sha256(title.read_bytes()).hexdigest(),'direct_missing_in_title_count':len(missing),'direct_missing_in_title':missing,'Battle_Main_linked':any(line.split()[-1]=='Battle_Main' for line in defined.splitlines()),'battle_executed':False,'resource_endian_validated':False,'platform_memory_mapping_validated':False,'note':'compiling C does not adapt PS2 addresses, resource structures, initialization or frame scheduling'}
(out/'battle-link-audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='direct_missing_in_title'},indent=2))
