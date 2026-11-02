#!/usr/bin/env python3
"""Read-only audit of the file IDs requested by Battle_Load for stage 0, chars 0/1.

This checks that disc entries exist and records their sizes. It does not
convert resources, initialize jobs, call Battle_Load, or authorize combat.
"""
import argparse
import ctypes as C
import json
import subprocess
import tempfile
from pathlib import Path


class Span(C.Structure):
    _fields_ = [("offset", C.c_uint64), ("size", C.c_uint64), ("directory", C.c_uint)]


class Disc(C.Structure):
    _fields_ = [("read", C.c_void_p), ("opaque", C.c_void_p), ("size", C.c_uint64), ("root", Span)]


Reader = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint64, C.c_void_p, C.c_size_t)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--cc", default="gcc")
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix="dr-battle-load-audit-") as tmp, args.iso.open("rb") as source:
        so = Path(tmp) / "disc.so"
        subprocess.run([args.cc, "-std=c17", "-O2", "-Wall", "-Wextra", "-Werror", "-fPIC", "-shared",
                        "-I" + str(project / "src"), str(project / "src/dr_disc.c"), "-o", str(so)], check=True)
        lib = C.CDLL(str(so))
        lib.dr_mount.argtypes = [C.POINTER(Disc), Reader, C.c_void_p, C.c_uint64]
        lib.dr_game_file.argtypes = [C.POINTER(Disc), C.c_char_p, C.POINTER(Span)]

        @Reader
        def read(_opaque, at, target, size):
            source.seek(at)
            data = source.read(size)
            if len(data) != size:
                return -1
            C.memmove(target, data, size)
            return 0

        disc = Disc()
        if lib.dr_mount(C.byref(disc), read, None, args.iso.stat().st_size):
            raise SystemExit("invalid ISO")
        requests = [
            ("common sound", 0x14A, None), ("stage sound", 0x14E, None),
            ("left voice", 0xBDA, None), ("right voice", 0xBDB, None),
            ("stage model", 0x171, 0x6CB800),
            ("left member", 8, 0x1000), ("right member", 11, 0x1000),
            ("left model", 0x590, None), ("left animations", 0x598, None),
            ("left effects", 0x599, None), ("right model", 0x59A, None),
            ("right animations", 0x5A2, None), ("right effects", 0x5A3, None),
        ]
        rows = []
        for label, file_id, buffer_size in requests:
            path = f"pzs3us1/{file_id - 1:05d}.bin"
            span = Span()
            found = lib.dr_game_file(C.byref(disc), path.encode(), C.byref(span)) == 0 and not span.directory
            rows.append({"label": label, "file_id": file_id, "path": path, "found": found,
                         "bytes": span.size if found else None,
                         "within_loader_buffer": None if not found or buffer_size is None else span.size <= buffer_size})
        result = {"scope": "Battle_Load initial files, stage 0 and characters 0/1",
                  "all_found": all(row["found"] for row in rows),
                  "known_buffer_limits_ok": all(row["within_loader_buffer"] is not False for row in rows),
                  "original_loader_executed": False, "resources_converted_for_original_loader": False,
                  "requests": rows}
        print(json.dumps(result, indent=2))
        return 0 if result["all_found"] and result["known_buffer_limits_ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
