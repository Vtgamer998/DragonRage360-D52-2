#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Inspect character effect technique packs without executing game code.

Input is an externally supplied effects file. The output contains sizes and
container shapes only, never resource bytes. Strict bounds match dr_container.c.
"""
import argparse
import json
import struct
from pathlib import Path


def entries(data, *, maximum=4096):
    if len(data) < 12:
        raise ValueError("container too small")
    count = struct.unpack_from("<I", data)[0]
    if not 1 <= count <= maximum or (count + 2) * 4 > len(data):
        raise ValueError("invalid count/table")
    words = struct.unpack_from(f"<{count + 1}I", data, 4)
    offsets = [x & ~3 for x in words]
    if offsets[0] < (count + 2) * 4 or offsets[-1] > len(data) or offsets != sorted(offsets):
        raise ValueError("invalid offsets")
    return [(offsets[i], offsets[i + 1], words[i] & 3) for i in range(count)]


def shape(data, depth):
    try:
        items = entries(data)
    except ValueError:
        return {"bytes": len(data), "container": False}
    result = {"bytes": len(data), "container": True, "count": len(items),
              "present": sum(a < b for a, b, _ in items),
              "flags": sorted(set(flag for a, b, flag in items if a < b))}
    if depth:
        result["members"] = [None if a == b else shape(data[a:b], depth - 1)
                             for a, b, _ in items]
    return result


def inspect(path, depth):
    data = path.read_bytes()
    if not 32 <= len(data) <= 4 * 1024 * 1024:
        raise ValueError("unexpected effects file size")
    outer = entries(data)
    if len(outer) != 6 or any(a == b or flag for a, b, flag in outer):
        raise ValueError("expected six uncompressed outer members")
    return {"file": path.name, "bytes": len(data),
            "techniques": [{"slot": i, **shape(data[a:b], depth)}
                           for i, (a, b, _) in enumerate(outer[:5])],
            "manager": shape(data[outer[5][0]:outer[5][1]], 0)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("effects", nargs="+", type=Path)
    parser.add_argument("--depth", type=int, default=1, choices=range(4))
    args = parser.parse_args()
    print(json.dumps([inspect(p, args.depth) for p in args.effects], indent=2))


if __name__ == "__main__":
    main()
