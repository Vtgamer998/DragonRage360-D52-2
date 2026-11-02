#!/usr/bin/env python3
"""Xbox 360 counterpart of port/tools/strip_data.py of the PC port: takes the game's own data out of the linked
program and writes the list that xb_gamedata.c reads (format BT3X, see xb_gamedata.h).

Difference from the PC tool: the PS2's data is little-endian and the Xbox 360 program is big-endian. A table the
assembler built from `.word` is in the program with its bytes reversed per word, so "bytes equal to the user's
executable" no longer finds it. Here a run is data when it equals the source *after reversing each unit of 1, 2, 4
or 8 bytes*; the unit width is written into the list and applied again by the loader. Where the program holds
bytes that match under no width (addresses the linker filled in, build padding) they stay, as on the PC.

    xb_strip_data.py --exe build/dragonrage.elf --prefix powerpc-xenon-elf- --data gamedata --out build/release

NOT tested against a real Xbox link (none was available): the matching core, match_runs(), is tested by
tools/test_xb_strip.py; the part that finds the data objects (objdump / nm / the linker map, the object naming
`obj_data*/cod_XXXXXX.o`) is the PC tool's code unchanged, so it assumes the Xbox build names and maps them the same
way. What the tool can NOT do: patch gPortDataStripped inside an encrypted/compressed XEX. Run it on the ELF /
before the XEX is made, or leave the flag at 2 (the loader then only needs the list to exist).
"""
import argparse, pathlib, re, struct, subprocess, sys

ROM_BASE, ROM_END, DBZP_BASE = 0x100000, 0x2FF180, 0x334C00
MIN_RUN = 4  # shorter runs of equal bytes are left (a coincidence inside an address)
WIDTHS = (8, 4, 2, 1)

def flat_image(elf_bytes):
    b = elf_bytes
    shoff, = struct.unpack_from("<I", b, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", b, 0x2E)
    out = bytearray(ROM_END - ROM_BASE)
    for i in range(shnum):
        _, kind, flags, addr, off, size = struct.unpack_from("<6I", b, shoff + i * shentsize)
        if flags & 2 and kind != 8 and size and ROM_BASE <= addr and addr + size <= ROM_END:
            out[addr - ROM_BASE:addr - ROM_BASE + size] = b[off:off + size]
    return bytes(out)

def fnv(data, h=0xCBF29CE484222325):
    for byte in data:
        h = ((h ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h

def swapped(b, w):
    return b if w == 1 else b"".join(b[i:i + w][::-1] for i in range(0, len(b), w))

def match_runs(image, src, size, base_addr, src_off):
    """image[0:size]: the piece as it is in the Xbox program (starts at program address base_addr); src[src_off:
    src_off+size]: the same piece in the PS2 source. Returns (records, taken_source_bytes, zeroed_image_ranges):
    records = (program address, source offset, length, width). A unit counts when its bytes equal the source's with
    the unit reversed; the widest width whose aligned unit (alignment relative to the PS2 address, i.e. to src_off
    here, as gen_data.py keeps a piece at its PS2 offset mod 16) matches wins. Units of zeros are not taken (they
    are what the stripped program holds anyway) and break a run."""
    units = []  # (offset in piece, width) of every unit that matches, greedy widest first, aligned
    i = 0
    while i < size:
        took = False
        for w in WIDTHS:
            if (src_off + i) % w == 0 and i + w <= size:
                a = bytes(image[i:i + w])
                s = bytes(src[src_off + i:src_off + i + w])
                if a == swapped(s, w) and any(a):
                    units.append((i, w))
                    i += w
                    took = True
                    break
        if not took:
            i += 1
    # merge neighbouring units of the same width into runs
    records, taken, zero = [], bytearray(), []
    k = 0
    while k < len(units):
        start, w = units[k]
        end = start + w
        j = k + 1
        while j < len(units) and units[j][0] == end and units[j][1] == w:
            end += w
            j += 1
        if end - start >= MIN_RUN:
            records.append((base_addr + start, src_off + start, end - start, w))
            taken += src[src_off + start:src_off + end]
            zero.append((start, end))
        k = j
    return records, bytes(taken), zero

def main():
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True, help="the linked Xbox program (ELF)")
    ap.add_argument("--prefix", default="powerpc-xenon-elf-", help="binutils prefix (objdump, nm)")
    ap.add_argument("--data", default="gamedata", help="folder with disc/SLUS_216.78 and disc/BIN/DBZP.BIN")
    ap.add_argument("--symbols", default="config/symbols", help="the decompilation's symbol files")
    ap.add_argument("--out", default="build/release")
    ap.add_argument("--flag-symbol", default="gPortDataStripped")
    a = ap.parse_args()
    exe = pathlib.Path(a.exe)
    image = bytearray(exe.read_bytes())
    data = pathlib.Path(a.data)
    src = [flat_image((data / "disc/SLUS_216.78").read_bytes()), (data / "disc/BIN/DBZP.BIN").read_bytes()]
    base = [ROM_BASE, DBZP_BASE]
    secs = []
    lines = subprocess.run([a.prefix + "objdump", "-h", str(exe)], capture_output=True, text=True).stdout.splitlines()
    for k, l in enumerate(lines):
        m = re.match(r"\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)", l)
        if m and k + 1 < len(lines) and "CONTENTS" in lines[k + 1]:
            secs.append((int(m.group(3), 16), int(m.group(5), 16), int(m.group(2), 16)))
    def file_off(addr):
        for va, off, size in secs:
            if va <= addr < va + size:
                return off + addr - va
        return None
    known = {}
    for p in pathlib.Path(a.symbols).glob("*.txt"):
        for l in p.read_text().splitlines():
            m = re.match(r"(\w+)\s*=\s*0x([0-9A-Fa-f]+)", l)
            if m:
                known[m.group(1)] = int(m.group(2), 16)
    nm = subprocess.run([a.prefix + "nm", str(exe)], capture_output=True, text=True).stdout.splitlines()
    syms = []
    for l in nm:
        p = l.split()
        if len(p) == 3:
            m = re.fullmatch(r"D_([0-9A-F]{8})", p[2])
            ps2 = int(m.group(1), 16) if m else known.get(p[2])
            if ps2:
                syms.append((int(p[0], 16), ps2))
    syms.sort()
    pieces = []
    for l in open(str(exe) + ".map"):
        m = re.match(r"^\s*(\.\S+)?\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+\S*(obj_data\w*/\S+\.o)\s*$", l)
        if m and m.group(1) in (".data", ".rodata") and int(m.group(3), 16):
            pieces.append((int(m.group(2), 16), int(m.group(3), 16), m.group(4)))
    records, taken, total, odd = [], bytearray(), 0, []
    for start, size, name in pieces:
        total += size
        inside = [(h, p) for h, p in syms if start <= h < start + size]
        deltas = {h - p for h, p in inside}
        m = re.search(r"/cod_([0-9A-F]{6,8})\.", name)
        if m:
            ps2 = int(m.group(1), 16) + (ROM_BASE if len(m.group(1)) == 6 else 0)
            deltas.add(start + (ps2 & 15) - ps2)
        best = None
        off = file_off(start)
        for delta in deltas:
            which = 1 if start - delta >= DBZP_BASE else 0
            so = start - delta - base[which]
            if so < 0 or so + size > len(src[which]) or off is None:
                continue
            recs, tk, zero = match_runs(image[off:off + size], src[which], size, start, so)
            score = len(tk)
            if best is None or score > best[0]:
                best = (score, which, recs, tk, zero, off)
        if best is None or (best[0] < size // 2 and size > 64):
            odd.append((name, size, best[0] if best else -1))
            continue
        _, which, recs, tk, zero, off = best
        for z0, z1 in zero:
            image[off + z0:off + z1] = bytes(z1 - z0)
        for (addr, so, ln, w) in recs:
            records.append((addr, which, so, ln, w))
        taken += tk
    # A zero/incomplete manifest cannot restore the skeleton tables of this
    # project. Never publish a release whose data was silently left missing.
    if not pieces or not records or odd:
        raise SystemExit("No complete data layout found; no release emitted. "
                         "The skeleton build needs explicit typed data metadata; "
                         "byte matching does not reconstruct missing table values.")
    flag = next((int(l.split()[0], 16) for l in nm if l.endswith(" " + a.flag_symbol)), None)
    if flag is not None and file_off(flag) is not None:
        image[file_off(flag):file_off(flag) + 4] = struct.pack(">I", 1)  # big-endian: the program's own byte order
    else:
        print("warning: %s not found in the program's file: it will not insist on its list" % a.flag_symbol)
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    prog = out / exe.name
    prog.write_bytes(image)
    head = struct.pack("<4sIQ", b"BT3X", len(records), fnv(bytes(taken)))
    (out / (exe.stem + ".dat")).write_bytes(head + b"".join(struct.pack("<5I", *r) for r in records))
    by_w = {w: sum(r[3] for r in records if r[4] == w) for w in WIDTHS}
    print(f"{len(pieces)} data objects, {total} bytes; {len(taken)} bytes taken out in {len(records)} runs; by width: {by_w}")
    for name, size, same in odd:
        print(f"  NOT matched to the game's executable: {name} ({size} bytes, {same} matching)")

if __name__ == "__main__":
    main()
