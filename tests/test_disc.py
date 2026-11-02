"""Synthetic ISO/AFS fixtures only. No copyrighted game data."""
import pathlib
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
EXE = ROOT / 'build/disc-probe'
SECTOR = 2048

def dual32(n):
    return struct.pack('<I', n) + struct.pack('>I', n)

def rec(name, lba, size, directory=False):
    r = bytearray(33 + len(name) + (len(name) % 2 == 0))
    r[0] = len(r)
    r[2:10] = dual32(lba)
    r[10:18] = dual32(size)
    r[25] = 2 if directory else 0
    r[28:32] = b'\x01\x00\x00\x01'
    r[32] = len(name)
    r[33:33+len(name)] = name
    return r

def fixture():
    data = bytearray(32 * SECTOR)
    pvd = bytearray(SECTOR)
    pvd[:7] = b'\x01CD001\x01'
    pvd[80:88] = dual32(32)
    pvd[128:132] = b'\x00\x08\x08\x00'
    pvd[156:190] = rec(b'\0', 20, SECTOR, True)
    data[16*SECTOR:17*SECTOR] = pvd
    def directory(lba, rows):
        b = rec(b'\0', lba, SECTOR, True) + rec(b'\1', 20, SECTOR, True)
        b += b''.join(rows)
        data[lba*SECTOR:lba*SECTOR+len(b)] = b
    directory(20, [rec(b'SLUS_216.78;1', 24, 4), rec(b'BIN', 21, SECTOR, True), rec(b'DATA', 22, SECTOR, True)])
    directory(21, [rec(b'DBZP.BIN;1', 25, 4)])
    directory(22, [rec(b'PZS3US0.AFS;1', 26, 80)])
    data[24*SECTOR:24*SECTOR+4] = b'TEST'
    data[25*SECTOR:25*SECTOR+4] = b'MENU'
    data[26*SECTOR:26*SECTOR+24] = b'AFS\0' + struct.pack('<IIIII', 2, 64, 5, 72, 4)
    data[26*SECTOR+64:26*SECTOR+69] = b'HELLO'
    data[26*SECTOR+72:26*SECTOR+76] = b'NEXT'
    return data

class DiscTests(unittest.TestCase):
    def run_image(self, data, rel=None):
        with tempfile.TemporaryDirectory() as td:
            path = pathlib.Path(td)/'synthetic.iso'
            path.write_bytes(data)
            return subprocess.run([str(EXE), str(path)] + ([rel] if rel else []), capture_output=True)

    def test_identify(self):
        self.assertEqual(self.run_image(fixture()).returncode, 0)

    def test_loose_file_case_and_version(self):
        self.assertEqual(self.run_image(fixture(), 'disc/bin/dbzp.bin').stdout, b'MENU')

    def test_afs_entry_is_bounded(self):
        r = self.run_image(fixture(), 'pzs3us0/00000.bin')
        self.assertEqual((r.returncode, r.stdout), (0, b'HELLO'))
        self.assertEqual(self.run_image(fixture(), 'pzs3us0/00001.bin').stdout, b'NEXT')

    def test_missing_entry(self):
        self.assertNotEqual(self.run_image(fixture(), 'pzs3us0/00002.bin').returncode, 0)

    def test_truncated(self):
        self.assertNotEqual(self.run_image(fixture()[:17000]).returncode, 0)

    def test_corrupt_endian_pair(self):
        b = fixture(); b[16*SECTOR+84] ^= 1
        self.assertNotEqual(self.run_image(b).returncode, 0)

    def test_directory_invalid_length(self):
        b = fixture(); b[20*SECTOR+68] = 3
        self.assertNotEqual(self.run_image(b).returncode, 0)

    def test_afs_out_of_bounds(self):
        b = fixture(); struct.pack_into('<I', b, 26*SECTOR+8, 0xfffffff0)
        self.assertNotEqual(self.run_image(b, 'pzs3us0/00000.bin').returncode, 0)

    def test_afs_overlapping_table(self):
        b = fixture(); struct.pack_into('<I', b, 26*SECTOR+8, 8)
        self.assertNotEqual(self.run_image(b, 'pzs3us0/00000.bin').returncode, 0)

    def test_afs_oversized_table(self):
        b = fixture(); struct.pack_into('<I', b, 26*SECTOR+4, 0xffffffff)
        self.assertNotEqual(self.run_image(b, 'pzs3us0/00000.bin').returncode, 0)

    def test_multiextent_rejected(self):
        b = fixture(); b[22*SECTOR+68+25] |= 0x80
        self.assertNotEqual(self.run_image(b, 'pzs3us0/00000.bin').returncode, 0)

    def test_traversal_rejected(self):
        self.assertNotEqual(self.run_image(fixture(), 'disc/../SLUS_216.78').returncode, 0)

    def test_offset_above_four_gib(self):
        # Sparse image; only a few real sectors are written. Tests uint64 callback offsets.
        high = 0x200001
        with tempfile.TemporaryDirectory() as td:
            p = pathlib.Path(td)/'large.iso'
            b = fixture()
            b[16*SECTOR+80:16*SECTOR+88] = dual32(high+1)
            b[21*SECTOR+68+2:21*SECTOR+68+10] = dual32(high)
            with p.open('wb') as f:
                f.write(b); f.seek(high*SECTOR); f.write(b'HIGH'); f.truncate((high+1)*SECTOR)
            r = subprocess.run([str(EXE),str(p),'disc/BIN/DBZP.BIN'],capture_output=True)
            self.assertEqual((r.returncode,r.stdout),(0,b'HIGH'))

if __name__ == '__main__':
    unittest.main(verbosity=2)
