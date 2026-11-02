#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Strict original Battle_Main link attempt; never generates stubs or an XEX.

Builds original C (including nonmatching C attempts enabled by portsrc), the
actual portable VU0/arithmetic sources, and upstream's data TABLE SHAPES.
Those shapes do NOT contain game values. A successful link alone would still
need a native boot, endian-aware data initialization and platform adaptation.
The existing launcher and default.xex are never overwritten.
"""
from pathlib import Path
import argparse,concurrent.futures,hashlib,json,re,subprocess,sys

PIN='3ca419c7966b11438e2dc3c9a7e1914f102ed545'
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('upstream',type=Path);p.add_argument('compiler',type=Path)
    p.add_argument('xsdk',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--jobs',type=int,default=4)
    p.add_argument('--root',action='append',default=[],help='additional diagnostic roots, e.g. Game_Main; never starts the game')
    p.add_argument('--float-mode',choices=['legacy-soft','native-ps2'],default='legacy-soft')
    p.add_argument('--host-cxx',default='g++')
    p.add_argument('--plugin-include',action='append',default=[])
    p.add_argument('--platform',action='store_true')
    p.add_argument('--fixed-battle',action='store_true',help='offline stage-0/full-screen policy; optional extra roots are diagnostics only, no native boot implied')
    p.add_argument('--typed-catalog',type=Path,help='compiled generated catalog; never implies complete typing or native boot')
    p.add_argument('--host-audit',action='store_true',help='compile C only on host; never link or claim PPC validation')
    a=p.parse_args();repo=a.upstream.resolve();cc=a.compiler.resolve();sdk=a.xsdk.resolve();out=a.output.resolve()
    roots=list(dict.fromkeys(['Battle_Main',*a.root]))
    if a.fixed_battle and not a.platform:p.error('--fixed-battle requires --platform')
    if any(not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*',r) for r in roots):p.error('invalid root symbol')
    project=Path(__file__).resolve().parents[2];out.mkdir(parents=True,exist_ok=True)
    from platform_sources import reviewed_sources
    platform_sources=reviewed_sources(project)
    import float_build
    plugin=float_build.build_plugin(cc,project,out,a.host_cxx,a.plugin_include) if a.float_mode=='native-ps2' else None
    obj=out/'objects';gen=out/'prepared';logs=out/'logs'
    for directory in [obj,gen,logs]:directory.mkdir(exist_ok=True)
    commit=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip()
    if commit!=PIN:raise SystemExit('requires pinned upstream '+PIN)
    sys.path.insert(0,str(repo/'port/tools'))
    import portsrc,eeconst
    sources=[f for f in portsrc.sources() if f.relative_to(repo).as_posix() not in ['src/sys/dma.c']]
    # Original replacements used by the upstream portable build. No Sony/SDL
    # platform stub module is included to make a missing platform appear ready.
    sources.append(project/'engine/combat/dma_tex.c')
    if a.fixed_battle:sources.append(project/'engine/combat/offline_profile.c')
    sources += [repo/'src/port/vu0_a.c',repo/'src/port/vu0_b.c']
    sources += [repo/'port/src'/name for name in ['softfloat_ps2.c','mathf_pc.c','plat_fastvec.c','plat_libm.c']]
    if plugin:
        sources.remove(repo/'port/src/plat_libm.c')
        sources.append(project/'engine/combat/math_bridge.c')
    if a.platform:
        sources += platform_sources
    typed_catalog=a.typed_catalog.resolve() if a.typed_catalog else None
    if typed_catalog:
        if not a.platform: p.error('--typed-catalog requires --platform')
        sources.append(typed_catalog)
    sources += sorted((repo/'port/third_party/newlib_libm').glob('*.c'))
    sources += sorted((repo/'port/data').glob('*.s'))
    if a.host_audit:sources=[f for f in sources if f.suffix!='.s']
    common=([] if a.host_audit else ['-mcpu=cell'])+['-fno-pic','-ffunction-sections','-fdata-sections','-O2','-g','-ffp-contract=off','-fno-strict-aliasing','-fcommon','-fno-builtin']
    includes=['-I'+str(repo/'include'),'-I'+str(repo/'port/include'),'-I'+str(repo/'port/src'),
              '-I'+str(project/'src'),'-I'+str(project/'engine/graphics'),'-I'+str(project/'engine/platform/platform'),'-I'+str(sdk/'include')]
    def compile_one(f):
        rel='generated/typed_catalog.c' if f==typed_catalog else f.relative_to(repo).as_posix() if f.is_relative_to(repo) else f.relative_to(project).as_posix()
        local={'src/sys/movie.c':'engine/combat/movie.c','src/main.c':'engine/combat/main.c','src/battle/battle.c':'engine/combat/battle.c','src/sys/bpe.c':'engine/combat/bpe.c','src/sys/heap.c':'engine/combat/heap.c','src/sys/gfx.c':'engine/combat/gfx.c'}
        input_file=project/local[rel] if rel in local and (project/local[rel]).exists() else f
        input_bytes=input_file.read_bytes()
        lowered=False;fixed_omissions=[]
        key=rel.replace('/','_').replace('.','_')
        target=obj/(key+'.o');log=logs/(key+'.txt');enabled=[];missing=[]
        if f.suffix=='.s':
            asm_source=f
            if a.platform:
                import native_data_sections
                if rel in native_data_sections.RULES:
                    asm_source=gen/(key+'.s')
                    asm_source.write_text(native_data_sections.prepare(rel,input_bytes.decode()))
            cmd=[str(cc),*common,'-c',str(asm_source),'-o',str(target)]
            run=subprocess.run(cmd,capture_output=True,text=True)
        else:
            game=rel.startswith(('src/sys/','src/battle/','src/menu/')) or rel in ['src/main.c','engine/combat/dma_tex.c']
            flags=[*common,'-std=gnu89' if game else '-std=gnu99',*includes,'-I'+str(f.parent)]
            hard=f.name in ['plat_fastvec.c','plat_libm.c','math_bridge.c'] or rel.startswith('engine/platform/') or f==typed_catalog
            if not hard and not plugin:flags+=['-msoft-float']
            if plugin:
                flags+=['-mhard-float']
                if not hard and f.name!='softfloat_ps2.c':
                    flags+=['-fplugin='+str(plugin)];lowered=True
                if f.name=='softfloat_ps2.c':flags+=float_build.helper_flags()
            if a.platform:flags+=['-DPORT_BIGENDIAN','-DXB_USE_XAM','-DXB_USE_NTALLOC','-DXB_USE_NTDELAY']
            if game:
                if a.fixed_battle:flags+=['-DDR_FIXED_BATTLE']
                text,enabled,missing=portsrc.enable_attempts(input_bytes.decode())
                if a.fixed_battle:
                    import fixed_profile_source
                    text,fixed_omissions=fixed_profile_source.transform(rel,text)
                source=gen/(key+'.c');source.write_text(text)
                flags+=['-include',str(repo/'port/include/port_compat.h'),'-include',str(repo/'port/include/port_libm.h')]
                if rel=='src/main.c':flags+=['-Dmain=dr_ps2_original_main']
            else:
                source=f
                if rel.startswith('src/port/'):
                    flags+=['-include','math.h','-include',str(repo/'port/src/vu0_names.h'),'-DREF_VU0_EXTERN_ARITH','-include',str(repo/'port/include/port_libm.h')]
                if 'newlib_libm/' in rel:flags+=['-include',str(repo/'port/third_party/newlib_libm/newlib_shim.h'),'-include',str(project/'engine/combat/newlib_compat.h')]
                if f.name=='mathf_pc.c':flags+=['-include',str(repo/'port/include/port_libm.h')]
            # Preserve the upstream literal transformation instead of assuming
            # the host compiler rounds the PS2's constants in the same way.
            pre=subprocess.run([str(cc),*flags,'-E',str(source)],capture_output=True,text=True)
            if pre.returncode:run=pre
            else:
                prepared=gen/(key+'.i');prepared.write_text(eeconst.transform(pre.stdout) if game or (not hard and not rel.startswith('src/port/')) else pre.stdout)
                # Forced includes were already expanded in preprocessing.
                stripped=[];skip=False
                for flag in flags:
                    if skip:skip=False;continue
                    if flag=='-include':skip=True;continue
                    stripped.append(flag)
                cmd=[str(cc),*stripped,'-fpreprocessed','-c',str(prepared),'-o',str(target)]
                run=subprocess.run(cmd,capture_output=True,text=True)
        log.write_text(run.stdout+run.stderr)
        unchanged=input_file.read_bytes()==input_bytes
        if run.returncode or not unchanged:target.unlink(missing_ok=True)
        return {'source':rel,'original_source':str(input_file),'source_sha256':hashlib.sha256(input_bytes).hexdigest(),
                'fixed_PC_hooks_omitted':fixed_omissions,'native_fighter_bridges_and_vector_fixes':bool(a.fixed_battle and rel=='src/battle/bobj_a.c'),
                'compiled':run.returncode==0 and unchanged,'source_unchanged_during_compile':unchanged,
                'software_arithmetic_lowered':lowered,'object':str(target),'log':str(log),
                'C_attempts_enabled':enabled,'functions_without_C':missing}
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1,min(a.jobs,8))) as pool:
        rows=list(pool.map(compile_one,sources))
    if a.host_audit:
        report={'host_audit_only':True,'PPC_compiled':False,'linked':False,'files':rows,
                'compiled':sum(r['compiled'] for r in rows),'total':len(rows)}
        (out/'host-audit.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps({k:v for k,v in report.items() if k!='files'}))
        for row in rows:
            if not row['compiled']:print(row['source']+': '+Path(row['log']).read_text()[:3000])
        return 0 if report['compiled']==report['total'] else 1
    data_section_audit=None
    if a.platform and all(r['compiled'] for r in rows):
        import native_data_sections
        data_section_audit=native_data_sections.audit(rows,cc,out)
    overlay_ld=sdk/'src/xsdk.ld'
    if a.platform and all(r['compiled'] for r in rows):
        import build_overlay_catalog
        source,overlay_ld=build_overlay_catalog.generate(rows,cc,sdk,out)
        target=obj/'native_overlay_catalog.o';log=logs/'native_overlay_catalog.txt'
        result=subprocess.run([str(cc),*common,'-mhard-float','-std=gnu99',*includes,'-c',str(source),'-o',str(target)],capture_output=True,text=True)
        log.write_text(result.stdout+result.stderr)
        rows.append({'source':'generated/overlay_catalog.c','source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'compiled':result.returncode==0,'source_unchanged_during_compile':True,'software_arithmetic_lowered':False,'object':str(target),'log':str(log),'C_attempts_enabled':[],'functions_without_C':[]})
    nm=cc.with_name(cc.name[:-3]+'nm')
    compiled=[Path(row['object']) for row in rows if row['compiled']]
    arithmetic_audit={'objects':0,'native_arithmetic':[],'runtime_verified':False}
    if plugin:
        objdump=cc.with_name(cc.name[:-3]+'objdump')
        for row in rows:
            if not row['compiled'] or not row['software_arithmetic_lowered']:continue
            listing=subprocess.check_output([str(objdump),'-d',row['object']],text=True)
            hits=re.findall(r'\t((?:fadd\w*|fsub\w*|fmul\w*|fdiv\w*|fcmp\w*|fcti\w*|fcfid\w*|frsp\w*|fsqrt\w*|fmadd\w*|fmsub\w*|fnmadd\w*|fnmsub\w*))\s',listing)
            arithmetic_audit['objects']+=1
            if hits:arithmetic_audit['native_arithmetic'].append({'source':row['source'],'instructions':sorted(set(hits))})
    # Record the definitions actually provided by the engine translation units.
    definitions={}
    for path in compiled:
        listing=subprocess.check_output([str(nm),'--defined-only',str(path)],text=True)
        for line in listing.splitlines():
            fields=line.split()
            if len(fields)==3 and fields[1].isupper():definitions.setdefault(fields[2],[]).append(path.name)
    (out/'definitions.json').write_text(json.dumps(definitions,indent=2)+'\n')
    report={'diagnostic_roots':roots,'upstream':PIN,'compiled':len(compiled),'total':len(rows),'files':rows,
            'fixed_battle_policy':a.fixed_battle,'typed_catalog_linked':typed_catalog is not None,
            'reviewed_platform_source_count':len(platform_sources),'platform_source_inventory':'engine/platform/platform-sources.txt','float_mode':a.float_mode,'platform_04_sources':a.platform,
            'arithmetic_audit':arithmetic_audit,
            'Battle_Main_C_object_compiled':'Battle_Main' in definitions,
            'stubs_generated':False,'native_data_sections_audited':data_section_audit is not None,'data_values_loaded':False,'resource_endian_adapted':False,
            'native_boot_integrated':False,'battle_executed':False,'XEX_generated':False}
    if len(compiled)!=len(rows):
        report['link']='NOT_ATTEMPTED_COMPILE_FAILURE'
        # Continue with a diagnostic link of successful objects, explicitly
        # labelled incomplete. Its failure must not be called a full closure.
    native=[]
    makefile=(project/'Makefile').read_text()
    line=re.search(r'^OBJECTS := (.+)$',makefile,re.M)
    if not line:raise SystemExit('native OBJECTS list missing')
    native=[project/name for name in line.group(1).split()]
    # Makefile's separately appended platform objects are rebuilt above when
    # --platform is selected; do not add duplicate definitions to the link.
    if not a.platform:
        native += [project/'build'/(f.stem+'.o') for f in platform_sources]
    if any(not f.exists() for f in native):raise SystemExit('build the native launcher first')
    title=out/'battle-link.elf';title.unlink(missing_ok=True)
    cmd=[str(cc),'-static','-nostdlib','-T',str(overlay_ld),
         '-Wl,--gc-sections,'+','.join('--undefined='+r for r in roots)+',-Map,'+str(out/'battle-link.map'),
         '-o',str(title),*map(str,native),*map(str,compiled),'-Wl,--start-group',
         str(sdk/'build/libxsdk.a'),'-lc','-lm','-lgcc','-Wl,--end-group']
    if typed_catalog:cmd.insert(1,'-Wl,--undefined=gXbTypedMainCatalog,--undefined=XbTypedData_LoadFromDisc,--undefined=XbTypedData_Load')
    (out/'link-arguments.json').write_text(json.dumps(cmd,indent=2)+'\n')
    link=subprocess.run(cmd,capture_output=True,text=True)
    (out/'link.txt').write_text(link.stdout+link.stderr)
    unresolved=sorted(set(re.findall(r"undefined reference to [`']([^`']+)'",link.stderr)))
    abi_conflicts=sorted(set(re.findall(r'failed to merge target specific data of file (.+)',link.stderr)))
    if link.returncode or len(compiled)!=len(rows):title.unlink(missing_ok=True)
    report.update({'link':'PASS' if not link.returncode else 'FAILED',
                   'ABI_conflict_objects':abi_conflicts,'ABI_conflict_count':len(abi_conflicts),
                   'safe_to_execute':False,'native_title_modified':False,
                   'all_sources_compiled':len(compiled)==len(rows),
                   'unresolved_count':len(unresolved),'unresolved':unresolved,
                   'scope':'reachable sections with explicit diagnostic_roots forced as link roots, upstream table shapes without values; existing launcher objects also present; not native boot or gameplay'})
    (out/'battle-integration.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ['files','unresolved']},indent=2),flush=True)
    for row in rows:
        if not row['compiled']:
            lines=Path(row['log']).read_text().splitlines()
            error=next((line for line in lines if 'error:' in line or 'Error:' in line),lines[0] if lines else 'compile failure')
            print(row['source']+': '+error)
    # Even a successful diagnostic link of a subset is not a valid engine build.
    return 1 if link.returncode or len(compiled)!=len(rows) or arithmetic_audit['native_arithmetic'] else 0
if __name__=='__main__':sys.exit(main())
