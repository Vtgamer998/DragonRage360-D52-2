#!/usr/bin/env python3
"""Tests match_runs() and the BT3X list against the loader's rules, with synthetic data (no game data needed)."""
import pathlib, struct, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from xb_strip_data import match_runs, swapped, fnv

# a PS2 piece (little-endian) of: 8 bytes of text, 3 words, 2 halves, a double, some bytes
src = bytearray(b"ABCDEFGH" + struct.pack("<3I", 0x11223344, 0x55667788, 0x99AABBCC) + struct.pack("<2H", 0x1234, 0x5678)
                + struct.pack("<d", 3.5) + b"\x01\x02\x03\x04\x05\x06\x07\x08")
# the same piece as an Xbox build assembles it (big-endian, per directive), with one word that the linker owns
xb = bytearray(b"ABCDEFGH" + struct.pack(">3I", 0x11223344, 0x55667788, 0x99AABBCC) + struct.pack(">2H", 0x1234, 0x5678)
               + struct.pack(">d", 3.5) + b"\x01\x02\x03\x04\x05\x06\x07\x08")
xb[8:12] = struct.pack(">I", 0xDEADBEEF)  # an address filled in by the linker: must stay
recs, taken, zero = match_runs(xb, src, len(src), 0x82000000, 0)
# rebuild the image the way the loader does and compare with the Xbox build
out = bytearray(len(xb))
for z0, z1 in zero:
    pass
for addr, so, ln, w in recs:
    out[addr - 0x82000000:addr - 0x82000000 + ln] = swapped(bytes(src[so:so + ln]), w)
rebuilt = bytearray(xb)
for z0,z1 in zero:
    rebuilt[z0:z1] = bytes(z1-z0)
for addr,so,ln,w in recs:
    rebuilt[addr-0x82000000:addr-0x82000000+ln] = swapped(bytes(src[so:so+ln]),w)
assert rebuilt == xb, "reloading must reproduce every original byte"
assert out[8:12] == bytes(4), "the linker's word must not be taken"
widths = sorted({w for *_, w in recs})
print("runs:", [(hex(a), l, w) for a, _, l, w in recs])
assert widths == [1, 2, 4, 8], widths
assert fnv(taken) == fnv(b"".join(bytes(src[so:so + ln]) for _, so, ln, _ in recs))
print("ok")
