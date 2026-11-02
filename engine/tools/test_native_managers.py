#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Original native boot under i386. Xbox SDK imports TRAP if called.
No function/callback is replaced in the port or in its data tables. The test
cannot prove Xbox CPU arithmetic, GPU, audio or a complete battle scene.
"""
from pathlib import Path
import argparse,concurrent.futures,subprocess,json,re,sys,os,hashlib,shlex
p=argparse.ArgumentParser();p.add_argument('upstream',type=Path);p.add_argument('xsdk',type=Path);p.add_argument('i386_root',type=Path);p.add_argument('qemu',type=Path);p.add_argument('native_build',type=Path);p.add_argument('catalog',type=Path);p.add_argument('resources',type=Path);p.add_argument('programs',type=Path);p.add_argument('output',type=Path);p.add_argument('--iso',type=Path,help='ISO USA local; defaults to stage-fixture.iso beside resources');a=p.parse_args()
e=Path(__file__).resolve().parents[1];project=e.parent;up=a.upstream.resolve();sdk=a.xsdk.resolve();root=a.i386_root.resolve();build=a.native_build.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
gen=out/'prepared';gen.mkdir(exist_ok=True);objs=out/'objects';objs.mkdir(exist_ok=True);logs=out/'logs';logs.mkdir(exist_ok=True)
sys.path.insert(0,str(up/'port/tools'));import portsrc,eeconst
from fixed_profile_source import transform
import native_data_sections,float_build
report=json.loads((build/'battle-integration.json').read_text());assert report['link']=='PASS'
flags=['-m32','-malign-double','-std=gnu99','-O1','-g','-fcommon','-fno-builtin','-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-ffp-contract=off','-fsanitize=undefined,bounds','-fno-omit-frame-pointer','-B'+str(root/'usr/lib32')+'/', '-B'+str(root/'usr/lib/gcc/x86_64-linux-gnu/13/32')+'/', '-isystem',str(root/'usr/include'),'-isystem',str(root/'usr/include/x86_64-linux-gnu'),'-isystem','/usr/include/x86_64-linux-gnu','-I'+str(up/'include'),'-I'+str(up/'port/include'),'-I'+str(up/'port/src'),'-I'+str(project/'src'),'-I'+str(e/'graphics'),'-I'+str(e/'capture/port/src/gs'),'-I'+str(e/'platform/platform'),'-I'+str(sdk/'include'),'-DPORT_BIGENDIAN','-DDR_FIXED_BATTLE','-DDR_XBOX360']
sources=[]
for row in report['files']:
 rel=row['source']
 if rel.startswith('generated/'):continue
 path=Path(row['original_source'])
 sources.append((rel,path))
# Native title objects have genuine portable C producers/graphics bridges.
makefile=(project/'Makefile').read_text();native=re.search(r'^OBJECTS := (.+)$',makefile,re.M)[1].split()
for obj in native:
 if obj in ['build/main.o']:continue
 match=re.search('^'+re.escape(obj)+r': ([^\s]+\.c)',makefile,re.M)
 path=project/(match[1] if match else 'src/'+Path(obj).stem+'.c')
 sources.append(('title/'+path.relative_to(project).as_posix(),path))
sources+=[('test/harness.c',e/'tests/test_native_managers.c'),('test/catalog.c',a.catalog.resolve())]
def compile_one(item):
 rel,path=item;key=rel.replace('/','_').replace('.','_');obj=objs/(key+'.o');log=logs/(key+'.txt');extra=[]
 if rel.startswith('port/data/'):
  text=native_data_sections.prepare(rel,path.read_text());source=gen/(key+'.s');source.write_text(text);cmd=['gcc','-m32','-c',str(source),'-o',str(obj)]
 else:
  source=path
  game=rel.startswith(('src/sys/','src/battle/','src/menu/')) or rel=='src/main.c' or rel=='engine/combat/dma_tex.c'
  if game:
   text,_,_=portsrc.enable_attempts(path.read_text());text,_=transform(rel,text);source=gen/(key+'.c');source.write_text(text)
   extra+=['-include',str(up/'port/include/port_compat.h'),'-include',str(up/'port/include/port_libm.h')]
   if rel=='src/main.c':extra+=['-Dmain=dr_ps2_original_main']
  if 'newlib_libm/' in rel:extra+=['-fno-sanitize=shift,signed-integer-overflow','-include',str(up/'port/third_party/newlib_libm/newlib_shim.h'),'-include',str(e/'combat/newlib_compat.h')]
  if rel.startswith('src/port/'):extra+=['-include','math.h','-include',str(up/'port/src/vu0_names.h'),'-DREF_VU0_EXTERN_ARITH','-include',str(up/'port/include/port_libm.h')]
  if path.name=='mathf_pc.c':extra+=['-include',str(up/'port/include/port_libm.h')]
  if path.name=='softfloat_ps2.c':extra+=float_build.helper_flags()
  # The generated rasterizer has thousands of specialised scalar loops. Keep
  # sanitizers, but avoid the very expensive optimizer pass in this CPU harness.
  if rel=='title/engine/capture/port/src/gs/gs_draw.c':extra+=['-O0']
  if path.name in ['xb_gamedata.c','xb_resconv.c']:extra+=['-DXB_HOST_TEST']
  # The native-source replacement itself still needs the source directory.
  cmd=['gcc',*flags,*extra,'-I'+str(path.parent),'-c',str(source),'-o',str(obj)]
 dep=objs/(key+'.d')
 if source.suffix!='.s':cmd[1:1]=['-MMD','-MF',str(dep)]
 def digest_now():
  h=hashlib.sha256(('compiler-dependencies-v1'+json.dumps(cmd)+source.read_text()).encode())
  if source.suffix!='.s':
   if not dep.exists():return None
   paths=shlex.split(dep.read_text().replace('\\\n',' ').split(':',1)[1])
   for name in sorted(set(paths)):
    file=Path(name)
    if not file.exists():return None
    h.update(name.encode());h.update(file.read_bytes())
  return h.hexdigest()
 stamp=objs/(key+'.sha256');digest=digest_now()
 if digest and obj.exists() and stamp.exists() and stamp.read_text()==digest:return {'source':rel,'compiled':True,'object':str(obj),'log':str(log)}
 r=subprocess.run(cmd,capture_output=True,text=True)
 if not r.returncode:stamp.write_text(digest_now() or '')
 log.write_text(r.stdout+r.stderr)
 return {'source':rel,'compiled':r.returncode==0,'object':str(obj),'log':str(log)}
sources.append(('test_common3_consumers.c',e/'tests/test_common3_consumers.c'))
with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:rows=list(pool.map(compile_one,sources))
(out/'compile.json').write_text(json.dumps(rows,indent=2)+'\n')
failed=[row for row in rows if not row['compiled']]
if failed:
 for row in failed:print(row['source']+'\n'+Path(row['log']).read_text()[-2500:])
 raise SystemExit('compilation failures: '+str(len(failed)))
objects=[row['object'] for row in rows]
link=['gcc','-m32','-B'+str(root/'usr/lib32')+'/', '-B'+str(root/'usr/lib/gcc/x86_64-linux-gnu/13/32')+'/', '-L'+str(root/'usr/lib32'),'-Wl,--sysroot='+str(root),'-Wl,--dynamic-linker='+str(root/'usr/lib32/ld-linux.so.2'),'-Wl,-rpath,'+str(root/'usr/lib32'),'-fsanitize=undefined,bounds',*objects,'-Wl,--gc-sections','-lm','-o',str(out/'managers')]
r=subprocess.run(link,capture_output=True,text=True);(out/'link.txt').write_text(r.stdout+r.stderr)
missing=sorted(set(re.findall(r"undefined reference to [`‘]([^'’]+)['’]",r.stderr)))
# Only host test transport imports are trapped. Missing engine/platform/game
# symbols remain link errors; no pretend implementation can make this pass.
allowed=[name for name in missing if re.fullmatch(r'(xsdk_[A-Za-z0-9_]+|Xam[A-Za-z0-9_]+|Nt[A-Za-z0-9_]+|Ke[A-Za-z0-9_]+|Mm[A-Za-z0-9_]+|Ex[A-Za-z0-9_]+|DbgPrint)',name)]
if allowed:
 trap=gen/'sdk_import_traps.c';trap.write_text('#include <stdio.h>\n#include <stdlib.h>\n'+''.join('__attribute__((noreturn)) void '+name+'(void){fputs("UNEXPECTED SDK CALL: '+name+'\\n",stderr);abort();}\n' for name in allowed))
 obj=objs/'sdk_import_traps.o';subprocess.run(['gcc',*flags,'-c',str(trap),'-o',str(obj)],check=True)
 link.insert(link.index('-Wl,--gc-sections'),str(obj));r=subprocess.run(link,capture_output=True,text=True);(out/'link.txt').write_text(r.stdout+r.stderr)
if r.returncode:
 print(r.stderr[-8000:]);raise SystemExit('strict host link failed')
(out/'result.json').unlink(missing_ok=True)
run=subprocess.run([str(a.qemu.resolve()),'-L',str(root),'-E','LD_LIBRARY_PATH='+str(root/'usr/lib32'),str(out/'managers'),str(a.resources.resolve()),str(a.programs.resolve()),str((a.iso or a.resources.parent/'stage-fixture.iso').resolve())],capture_output=True,text=True,env=dict(os.environ,UBSAN_OPTIONS='halt_on_error=1'))
(out/'test.txt').write_text(run.stdout+run.stderr);print(run.stdout+run.stderr)
if run.returncode:(out/'result.json').write_text(json.dumps({'result':'FAIL','returncode':run.returncode,'full_effect_scene_probe':bool(os.environ.get('XB_TEST_FULL_SCENE'))},indent=2)+'\n')
run.check_returncode()
result={'result':'PASS','host_execution':'QEMU-i386','original_full_roster_reset_and_AI_init':True,'original_object_Init_Term':True,'original_input_sample_remap_press_release':True,'typed_catalog_real_data_and_callbacks':True,'SDK_import_traps':allowed,'SDK_device':'simulated synchronous PCM sink','original_audio_cycle':True,'typed_catalog_native_host_byte_order':True,'original_File_Init_and_three_partitions':True,'original_stage_Init_Reset_Update_Term':True,'original_stage_culling':True,'original_stage_rigid_body':True,'original_navigation_short_route':True,'original_timer_synthetic_fixture':True,'stage_geometry':{'groups':7,'materials':34,'meshes':2243,'parts':1959,'nodes':172,'cells':30,'textures':25,'timers_in_actual_stage':0},'audio_frames':int(re.findall(r'(\d+) frames, (\d+) nonzero samples',run.stdout)[-1][0]),'nonzero_PCM_values':int(re.findall(r'(\d+) frames, (\d+) nonzero samples',run.stdout)[-1][1]),'real_stage_converters':39,'real_stage_original_Init_Update_Cull_Term':39,'real_stage_updates_each':12,'real_stage_animated_packs':6,'real_stage_animated_object_definitions':31,'real_stage_timers':19,'real_stage_timer_wraps':57,'real_stage_animation_matrix_samples_per_model':3,'real_stage_animation_corruption_rejections':8,'partial_GS_uploads_are_not_complete_RGBA':True,'common3_texture_subset':True,'common3_entries':637,'common3_textures':116,'common3_images_and_palette_entries':204,'common3_unprepared_entries':0,'common3_prepared_entries':637,'common3_original_set_consumers':24,'common3_original_sprite_consumers':50,'common3_original_particle_key_consumers':True,'common3_sprite_steps':2000,'common3_mesh_consumers':2,'common3_corruption_rejections':9,'common3_drawable_images':162,'common3_palette_only_entries':42,'full_effect_scene_initialized':run.stdout.count('original full effect scene term returned')==2,'original_scene_character_tasks':run.stdout.count('original effect characters created')==2,'original_scene_CPU_frames':run.stdout.count('original effect scene frame '),'original_EftTexSet_Load32_consumer':True,'MAPD_sections_vs_original_binder':20,'stage_corruption_rejections':7,'stage_geometry_corruption_rejections':5,'stage_restart_with_audio_device_open':True,'PowerPC_execution':False,'GPU_execution':False,'Battle_Update_or_Main':False,'sanitizers':'undefined,bounds; vendor newlib bit arithmetic excludes shift and signed-overflow checks'}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
