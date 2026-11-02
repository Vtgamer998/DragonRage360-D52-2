#!/usr/bin/env python3
"""Extract checked standalone reference VM from the patched, pinned upstream.
The interpreter and its arithmetic remain upstream code, not a new VU emulator.
"""
import pathlib,re,sys
repo=pathlib.Path(sys.argv[1]).resolve();engine=pathlib.Path(__file__).resolve().parents[1]
s=(repo/'port/src/gs/gs_vu1.c').read_text()
if 'static uint32_t vu_le' not in s:raise SystemExit('apply vu1-powerpc-endian.patch first')
s=s[:s.index('/* ---------------------------------------------------------------------------------------------- shader versions */')]
a=s.index('/* See Gs_StateKeep');b=s.index('static unsigned sUnknown',a);s=s[:a]+s[b:]
s=s.replace('#include "gs_internal.h"','#include "dr_vu1.h"\nstatic DrVuGif gif_callback;\nstatic void *gif_opaque;\nstatic int kick_error;')
s=s.replace('static unsigned sHle; /* program calls served by shader versions this frame */','')
# Original host convenience entry points become private implementation details.
for name in re.findall(r'GsVu1_\w+',s):
 s=s.replace(name,'legacy_'+name[6:].lower())
s=re.sub(r'(?m)^void legacy_', 'static void legacy_',s)
s=re.sub(r'(?m)^uint32_t legacy_', 'static uint32_t legacy_',s)
s=s.replace('const uint32_t *v)', 'const void *v)').replace('const uint32_t *words,','const void *words,').replace('const uint32_t *data)', 'const void *data)')
s=s.replace('&words[i], 4','(const uint8_t *)words + i * 4, 4')
s=s.replace('uint64_t lo, hi;', 'uint64_t lo;')
s=s.replace('        hi = (uint64_t)vu_le(&vu.mem[(a + 8) & 0x3FFF], 4) | (uint64_t)vu_le(&vu.mem[(a + 12) & 0x3FFF], 4) << 32;\n','')
s=s.replace('''        if (len + qw * 16 > sizeof(buf)) {
            break;
        }''','''        if (len + qw * 16 > sizeof(buf)) {
            kick_error = 1;
            return;
        }''')
s=s.replace('    Gs_Gif(buf, len / 16);','    if (gif_callback != NULL) gif_callback(gif_opaque, buf, len / 16);')
s=s.replace('static void run(uint32_t start)', 'static int run(uint32_t start, uint32_t budget)')
# Defined VU saturating conversion; no C cast of NaN or unrepresentable values.
s=s.replace('#include "dr_vu1.h"', '#include "dr_vu1.h"\n#include "dr_vu_ftoi.h"')
a=s.index('                static const float k[4] = {1.0f, 16.0f, 4096.0f, 32768.0f};')
b=s.index('                wr = &vu.vf[ft]; wmask = dest; break;',a)
s=s[:a]+"                static const unsigned fraction[4]={0,4,12,15};\n                res = vu.vf[ft];\n                EACH res.u[c]=dr_vu_ftoi(vu.vf[fs].u[c],fraction[sp-0x14]);\n"+s[b:]
s=s.replace('guard < 4000000','guard < budget')
s=s.replace('''            sProg[slot].kicks += sStatKicks - kbefore;
            return;''','''            sProg[slot].kicks += sStatKicks - kbefore;
            return DR_VU_OK;''')
a=s.index('    {\n        static int reported;',s.index('static int run('))
s=s[:a]+'    return DR_VU_BUDGET;\n}\n'
wrappers=(engine/'vu/dr_vu1_api.inc').read_text()
(engine/'vu/dr_vu1.c').write_text('/* SPDX-License-Identifier: AGPL-3.0-or-later\n * Generated from dfdx047/z3xox gs_vu1.c, revision 3ca419c7966b11438e2dc3c9a7e1914f102ed545,\n * after patches/vu1-powerpc-endian.patch. See tools/build_vu_reference.py.\n */\n'+s+'\n'+wrappers)
