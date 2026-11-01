#!/usr/bin/env python3
"""Embed a 4:3 PNG background as PS2 GS-native A1B5G5R5 PSMCT16 data."""
from pathlib import Path
from PIL import Image
import hashlib
import sys
import zlib

WIDTH, HEIGHT = 640, 480

def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: embed_background.py INPUT.png OUTPUT.h")
    source = Path(sys.argv[1])
    output = Path(sys.argv[2])
    if not source.is_file():
        raise SystemExit(f"background image not found: {source}")

    with Image.open(source) as im:
        im.load()
        w, h = im.size
        if w * HEIGHT != h * WIDTH:
            raise SystemExit(
                f"background must be 4:3 to avoid distortion; received {w}x{h}"
            )
        im = im.convert("RGB").resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)

    raw = bytearray()
    for r, g, b in im.getdata():
        # GS PSMCT16 layout used by the existing PCE Fast background path:
        # R in bits 0..4, G in 5..9, B in 10..14, opaque alpha in bit 15.
        pixel = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | 0x8000
        raw.append(pixel & 0xFF)
        raw.append((pixel >> 8) & 0xFF)

    compressed = zlib.compress(bytes(raw), level=9)
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    lines = [
        "/* Generated from assets/background.png by tools/embed_background.py.",
        f" * Source SHA-256: {digest}",
        " * Resized to 640x480; PS2 GS-native A1B5G5R5 / PSMCT16, opaque alpha.",
        " * zlib-compressed and embedded in the ELF; no background file is needed on USB.",
        " */",
        "#ifndef PCEFAST_EMBEDDED_BACKGROUND_H",
        "#define PCEFAST_EMBEDDED_BACKGROUND_H",
        f"#define PCEFAST_BG_WIDTH {WIDTH}",
        f"#define PCEFAST_BG_HEIGHT {HEIGHT}",
        "#define PCEFAST_BG_RAW_SIZE (PCEFAST_BG_WIDTH * PCEFAST_BG_HEIGHT * 2u)",
        "static const unsigned char pcefast_background_zlib[] = {",
    ]
    for start in range(0, len(compressed), 16):
        chunk = compressed[start:start + 16]
        lines.append("  " + ", ".join(f"0x{x:02x}" for x in chunk) + ",")
    lines.extend([
        "};",
        "#define PCEFAST_BG_ZLIB_SIZE ((unsigned long)sizeof(pcefast_background_zlib))",
        "#endif",
        "",
    ])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="ascii")

    # Verify the generated payload round-trips to the exact 640x480x2 buffer.
    check = zlib.decompress(compressed)
    if len(check) != WIDTH * HEIGHT * 2 or check != bytes(raw):
        raise SystemExit("embedded background verification failed")
    print(
        f"Embedded {source} ({w}x{h}) -> {output} "
        f"({WIDTH}x{HEIGHT}, {len(raw)} raw bytes, {len(compressed)} compressed bytes)"
    )

if __name__ == "__main__":
    main()
