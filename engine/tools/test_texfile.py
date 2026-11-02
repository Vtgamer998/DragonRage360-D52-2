#!/usr/bin/env python3
"""Real TexFile decoding: C sanitizer corpus and independent upstream Python reference."""
import argparse
import ctypes as C
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('iso', type=Path)
    p.add_argument('upstream', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--sanitize', action='store_true')
    p.add_argument('--reference-only', action='store_true', help='Skip C executables already run separately')
    a = p.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = a.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    # Test the same current sources linked into the title, not older snapshots.
    source_root = root.parent/'src'
    flags = ['gcc', '-std=c17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
             '-I'+str(source_root)]
    sources = [source_root/(n+'.c') for n in [
        'dr_texfile', 'dr_gs_texture', 'dr_disc', 'dr_container', 'dr_anim', 'dr_bpe', 'dr_skeleton']]
    if not a.reference_only:
        san = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if a.sanitize else []
        for test in ['test_texfile', 'texfile_real']:
            binary = out/test
            subprocess.run(flags+san+[str(root/'tests'/(test+'.c'))]+[str(s) for s in sources]+['-o',str(binary)],check=True)
            args = [str(binary)] + ([str(a.iso.resolve())] if test=='texfile_real' else [])
            result = subprocess.run(args, env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),text=True,capture_output=True)
            (out/(test+'.txt')).write_text(result.stdout+result.stderr)
            print(result.stdout+result.stderr,end=''); result.check_returncode()
    library = out/'texfile.so'
    subprocess.run(flags+['-shared','-fPIC','-Wl,-z,defs']+[str(s) for s in sources]+['-o',str(library)],check=True)
    lib = C.CDLL(str(library))
    Read = C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint64,C.c_void_p,C.c_size_t)
    class Span(C.Structure):
        _fields_=[('offset',C.c_uint64),('size',C.c_uint64),('directory',C.c_uint)]
    class Disc(C.Structure):
        _fields_=[('read',Read),('opaque',C.c_void_p),('size',C.c_uint64),('root',Span)]
    class Image(C.Structure):
        _fields_=[('width',C.c_uint32),('height',C.c_uint32),('psm',C.c_uint32),('bytes',C.c_size_t)]
    lib.dr_mount.argtypes=[C.POINTER(Disc),Read,C.c_void_p,C.c_uint64]
    lib.dr_game_file.argtypes=[C.POINTER(Disc),C.c_char_p,C.POINTER(Span)]
    lib.dr_texfile_decode.argtypes=[C.c_void_p,C.c_size_t,C.c_uint32,C.c_void_p,C.c_size_t,C.POINTER(Image)]
    spec=importlib.util.spec_from_file_location('reference_menu_font',a.upstream/'port/tools/menu_font.py')
    ref=importlib.util.module_from_spec(spec);spec.loader.exec_module(ref)
    f=a.iso.open('rb')
    @Read
    def read(_,offset,pointer,n):
        f.seek(offset);b=f.read(n)
        if len(b)!=n:return -1
        C.memmove(pointer,b,n);return 0
    disc=Disc();assert not lib.dr_mount(C.byref(disc),read,None,a.iso.stat().st_size)
    seen=set();compared=0;pixels=0
    def word(b,n):return struct.unpack_from('<I',b,n)[0]
    for ch in range(161):
        for skin in range(8):
            span=Span();path=f'pzs3us1/{0x590+ch*10+skin-1:05d}.bin'.encode()
            assert not lib.dr_game_file(C.byref(disc),path,C.byref(span))
            f.seek(span.offset);pack=f.read(span.size)
            if len(pack)==64:continue
            count=word(pack,0)
            for i in [0,*range(3,min(count,17))]:
                start,end=word(pack,4+i*4)&~3,word(pack,8+i*4)&~3;t=pack[start:end]
                if not t or word(t,0)&0xffff0000==0x60000000:continue
                for index in range(word(t,0)):
                    at=word(t,4)*4+index*64;ent=t[at:at+64];tex=struct.unpack_from('<Q',ent,48)[0]
                    psm=tex>>20&63;w,h=1<<(tex>>26&15),1<<(tex>>30&15);packet=word(ent,0)*4
                    key=(psm,w,h,word(ent,24),word(t,packet+48),word(t,packet+52),tex>>14&63)
                    if key in seen:continue
                    seen.add(key);rgba=C.create_string_buffer(w*h*4);info=Image()
                    assert not lib.dr_texfile_decode(t,len(t),index,rgba,len(rgba),C.byref(info))
                    expected=t[packet+96:packet+96+w*h*4] if psm==0 else ref.texture(t,index)
                    assert expected is not None and rgba.raw==expected,(ch,skin,i,index,key)
                    compared+=1;pixels+=w*h
    f.close()
    report={'result':'PASS','reference_layout_variants':compared,'reference_pixels':pixels,
            'reference':'pinned upstream port/tools/menu_font.py; CT32 linear source pixels',
            'gpu_execution':False,'body_material_mapping':False,'leak_detection':'disabled'}
    (out/'reference.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

if __name__=='__main__':main()
