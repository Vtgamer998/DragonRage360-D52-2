#!/usr/bin/env python3
"""Full vendored capture/core/backend integration with a mocked GPU, no pixel claim.
Usage: test_capture.py <xsdk> <out> [--sanitize]
"""
from pathlib import Path
import subprocess,sys,json,os
engine=Path(__file__).resolve().parents[1];root=engine.parent
sdk,out=(Path(p).resolve() for p in sys.argv[1:3]);out.mkdir(parents=True,exist_ok=True)
files=[engine/'tests/test_capture.c']
files += [engine/'capture/port/src/gs'/s for s in ['gs_draw.c','gs_core.c','gs_sony.c']]
files += [engine/'graphics'/s for s in ['dr_gs_backend.c','dr_gs_replay.c','dr_xenos.c','dr_targets.c','dr_gs_texture.c','dr_graphics_demo.c','dr_gs_stream.c','dr_gs_native.c','dr_dma_producer.c','dr_dma_engine.c','dr_gs_movie.c']]
files += [root/'src'/s for s in ['dr_capture_demo.c','dr_capture_platform.c','dr_vu1.c']]
cmd=['gcc','-std=gnu17','-O2','-Wall','-Wextra','-Werror','-DDR_XBOX360','-ffp-contract=off','-ffunction-sections','-fdata-sections','-Wl,--gc-sections']
cmd += ['-I'+str(p) for p in [engine/'graphics',engine/'capture/port/src/gs',sdk/'include',root/'src']]
cmd += list(map(str,files))+['-lm','-pthread','-o',str(out/'test-capture')]
if '--sanitize' in sys.argv:cmd[cmd.index('-O2')]='-O1';cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g']
subprocess.run(cmd,check=True)
r=subprocess.run([str(out/'test-capture')],capture_output=True,text=True)
(out/'capture.txt').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
(out/'capture.json').write_text(json.dumps({'result':'PASS','capture':'bounded DMA/VIF DIRECT + whole gs_draw.c and gs_core.c, reachable GS sections linked','GPU_execution':False,'battle_loop':False,'sanitizers':'ASan/UBSan' if '--sanitize' in sys.argv else 'off','all_original_combat_packet_producers_adapted':False,'adapted_producer_subset':'register/rect helpers; diagnostic uses Frame/Zbuf/Scissor/XYOffset/AD and Dma_Flush/ResetBuffers','native_GS_readback_test':'independent unaligned descriptors, two framebuffer halves and second target, exact target rejection, RGB crop rows and canaries, invalid descriptor/format/bounds, fence timeout; untouched output on failure; GPU mocked',
    'native_engine_probe':'Port_GsVif1Chain -> MPG/UNPACK/MSCAL/XGKICK -> actual GIF ALPHA state; Port_GsGifChannel; SetDefDBuff/SwapDBuff clear; GPU mocked','scope':'bounded canonical-LE DMA/VIF DIRECT -> GIF/register/VRAM -> real capture -> backend -> mock XSDK calls; lifecycle, endian boundaries, eight original filter/clamp selectors, DATE/FBA/snapshot descriptors, native VU shader selection/uniform uploads, shadow stencil reset, original captured outline/CLUT passes, unmerged self-target consumer resolves and raw-alpha binding, frozen effect IDs, rejected missing/wrong-owner snapshots and invalid CLUT/source before draw'},indent=2)+'\n')
