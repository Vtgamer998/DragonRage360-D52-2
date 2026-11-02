# SPDX-License-Identifier: GPL-3.0-or-later
"""Build support for the optional, explicit hard-ABI PS2 arithmetic pass."""
from pathlib import Path
import subprocess
RENAMES={
    '__addsf3':'addsf','__subsf3':'subsf','__mulsf3':'mulsf','__divsf3':'divsf','__negsf2':'negsf',
    '__eqsf2':'cmpsf','__nesf2':'nesf','__ltsf2':'ltsf','__lesf2':'lesf','__gtsf2':'gtsf','__gesf2':'gesf',
    '__unordsf2':'unordsf','__fixsfsi':'fixsf','__fixunssfsi':'fixunssf',
    '__floatsisf':'floatsf','__floatunsisf':'floatunssf'}
def helper_flags():
    return ['-D'+old+'=dr_ps2_'+new+'_bits' for old,new in RENAMES.items()]
def build_plugin(cc,project,out,host_cxx='g++',extra_includes=()):
    plugin_headers=Path(subprocess.check_output([str(cc),'-print-file-name=plugin'],text=True).strip())/'include'
    plugin=out/'ps2_float_pass.so'
    subprocess.run([host_cxx,'-shared','-fPIC','-fno-rtti','-std=c++17','-O2',
                    '-I'+str(plugin_headers),*['-I'+str(Path(i).resolve()) for i in extra_includes],
                    str(project/'engine/combat/ps2_float_pass.cc'),'-o',str(plugin)],check=True)
    return plugin
