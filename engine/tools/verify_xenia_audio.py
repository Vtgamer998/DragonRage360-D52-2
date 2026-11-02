#!/usr/bin/env python3
"""Check SDL's disk audio capture from Xenia; this is not an audible-device test."""
import argparse
import json
import math
import struct
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("raw", type=Path)
    p.add_argument("emulator_log", type=Path)
    args = p.parse_args()
    log = args.emulator_log.read_text(errors="replace")
    if "SDL_OpenAudioDevice() failed" in log:
        raise SystemExit("SDL did not open its disk audio device")
    data = args.raw.read_bytes()
    if not data or len(data) % 4:
        raise SystemExit("empty or unaligned audio data")
    count = 0
    peak = 0.0
    first = None
    for i, (sample,) in enumerate(struct.iter_unpack("<f", data)):
        if not math.isfinite(sample) or abs(sample) > 1.01:
            raise SystemExit(f"invalid float sample at index {i}")
        if sample:
            count += 1
            peak = max(peak, abs(sample))
            if first is None:
                first = i
    result = {
        "SDL_disk_device_opened": "AudioSystem::RegisterClient: client 0 registered successfully" in log,
        "raw_bytes": len(data),
        "nonzero_float_samples": count,
        "first_nonzero_sample": first,
        "peak": peak,
        "audible_hardware_verified": False,
    }
    print(json.dumps(result))
    return 0 if result["SDL_disk_device_opened"] and count else 1


if __name__ == "__main__":
    raise SystemExit(main())
