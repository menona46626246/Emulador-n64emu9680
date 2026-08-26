#!/usr/bin/env python3
"""Generate a tiny, original N64 ROM used by the conformance smoke test."""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path


def build_rom() -> bytes:
    entry = 0x8000_0400
    payload = (
        0x2408_0001,  # addiu $t0, $zero, 1
        0x1000_FFFF,  # beq   $zero, $zero, -1
        0x0000_0000,  # nop (delay slot)
    )
    rom = bytearray(0x1000 + len(payload) * 4)

    def put32(offset: int, value: int) -> None:
        struct.pack_into(">I", rom, offset, value)

    put32(0x00, 0x8037_1240)
    put32(0x04, 0x0000_000F)
    put32(0x08, entry)
    put32(0x0C, 0x0000_144C)
    put32(0x10, 0x4E36_3445)
    put32(0x14, 0x4D55_2020)
    title = b"N64EMU SMOKE".ljust(20, b" ")
    rom[0x20:0x34] = title
    rom[0x38:0x40] = b"N64E\x00\x00\x00\x00"
    for index, word in enumerate(payload):
        put32(0x1000 + index * 4, word)
    return bytes(rom)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    data = build_rom()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    digest = hashlib.sha256(data).hexdigest()
    print(f"generated {args.output} ({len(data)} bytes, sha256={digest})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
