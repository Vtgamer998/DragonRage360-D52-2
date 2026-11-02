#!/usr/bin/env python3
"""Inspect an external Xenia window capture; do not confuse it with guest readback.

Capture the emulator window after the diagnostic UI appears, for example with
ImageMagick's `import -window root screen.png` under Xvfb. This proves only
the UI presentation, never a battle frame.
"""
import argparse
import json
from pathlib import Path

from PIL import Image


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("capture", type=Path)
    p.add_argument("guest_log", type=Path)
    args = p.parse_args()
    image = Image.open(args.capture).convert("RGB")
    log = args.guest_log.read_text(errors="replace")
    orange = (255, 138, 31)
    # The UI tile is 68x68 at (52,52) inside the guest. Window decorations
    # shift it down, so look for a solid interior rectangle in the upper left.
    match = None
    for y in range(30, min(150, image.height - 20)):
        for x in range(30, min(160, image.width - 20)):
            if image.getpixel((x, y)) == orange and all(
                image.getpixel((x + dx, y + dy)) == orange
                for dx, dy in ((10, 0), (0, 10), (20, 20))
            ):
                match = [x, y]
                break
        if match:
            break
    result = {
        "external_ui_pixel": "PASS" if match else "FAIL",
        "tile_sample": match,
        "guest_readback_black": "render: hero tile pixel FF000000" in log,
        "battle_frame_verified": False,
    }
    print(json.dumps(result, ensure_ascii=False))
    return 0 if match else 1


if __name__ == "__main__":
    raise SystemExit(main())
