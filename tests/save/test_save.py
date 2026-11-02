"""Host checks of exact save format and non-destructive export, no game execution."""
from pathlib import Path
import hashlib
import struct
import subprocess
import tempfile
import sys
exe=str(Path(sys.argv[1]).resolve())
root_project=Path(__file__).resolve().parents[2]
base=(root_project/'extras/save-desbloqueado/BASLUS-21678DBZT3').read_bytes()
assert len(base)==16384
get=lambda b,o:struct.unpack_from('<I',b,o)[0]
def checksum(b):
    s=sum(b[i]<<((i&1)*8) for i in range(8,len(b)))&65535
    return get(b,0)==s>>8 and get(b,4)==s&255
assert checksum(base)
assert get(base,0x3028)==9999999
assert int.from_bytes(base[0xc10:0xc28],'little')==(1<<161)-1
assert int.from_bytes(base[0xc28:0xc30],'little')==(1<<35)-1
assert all(x&1 for x in base[0x2ec8:0x2ec8+350])
with tempfile.TemporaryDirectory() as td:
    root=Path(td);imports=root/'save_import';imports.mkdir();src=imports/'BASLUS-21678DBZT3'
    data=bytearray(base);struct.pack_into('<I',data,0x2d40+0x18,23)
    data[0x1808:0x1810]=bytes(range(8));data[0x3100:0x3110]=b'PRESERVE-ME-1234'
    s=sum(data[i]<<((i&1)*8) for i in range(8,len(data)))&65535
    struct.pack_into('<II',data,0,s>>8,s&255)
    src.write_bytes(data);before=hashlib.sha256(data).hexdigest()
    r=subprocess.run([exe,td,'1'],capture_output=True,text=True);assert r.returncode==0,r.stdout
    export=root/'exports/save-001/BASLUS-21678DBZT3';new=export.read_bytes()
    assert checksum(new) and new[0x1808:0x1810]==data[0x1808:0x1810] and get(new,0x2d40+0x18)==23
    assert new[0x3100:0x3110]==data[0x3100:0x3110]
    assert hashlib.sha256(src.read_bytes()).hexdigest()==before
    r=subprocess.run([exe,td,'0'],capture_output=True,text=True);assert r.returncode==0
    assert (root/'exports/save-002/BASLUS-21678DBZT3').read_bytes()==base
    assert export.read_bytes()==new
    damaged=bytearray(data);damaged[0x100]^=1;src.write_bytes(damaged)
    r=subprocess.run([exe,td,'1'],capture_output=True,text=True);assert r.returncode==1
    assert not (root/'exports/save-003').exists()
    src.write_bytes(b'bad')
    r=subprocess.run([exe,td,'1'],capture_output=True,text=True);assert r.returncode==1
    assert not (root/'exports/save-003').exists()
print('PASS: unlocks/checksum; separate exports; originals, custom records and unknown fields preserved; bad size refused')
print('New save SHA-256:',hashlib.sha256(base).hexdigest())
