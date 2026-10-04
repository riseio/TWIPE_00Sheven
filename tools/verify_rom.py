#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

EXPECTED_SIZE = 0x2000000
EXPECTED_SHA256 = "72e3e7b4ff1615bc17336d3d10e18aa9342898db063ace68922a47f5e46c48b1"
EXPECTED_MAGIC = 0x80371240
EXPECTED_ENTRYPOINT = 0x80000400
EXPECTED_CRC1 = 0x033F4C13
EXPECTED_CRC2 = 0x319EE7A7
EXPECTED_TITLE = "TWINE"
EXPECTED_GAME_CODE = "NO7E"
EXPECTED_REVISION = 0

class RomValidationError(ValueError):
    pass

def parse_header(header: bytes) -> dict[str, int | str]:
    if len(header) != 0x40:
        raise RomValidationError(f"header is {len(header)} bytes; expected 64")
    return {
        "magic": int.from_bytes(header[0x00:0x04], "big"),
        "entrypoint": int.from_bytes(header[0x08:0x0C], "big"),
        "crc1": int.from_bytes(header[0x10:0x14], "big"),
        "crc2": int.from_bytes(header[0x14:0x18], "big"),
        "title": header[0x20:0x34].decode("ascii", "replace").rstrip(" \0"),
        "game_code": header[0x3B:0x3F].decode("ascii", "replace"),
        "revision": header[0x3F],
    }

def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as rom:
        for chunk in iter(lambda: rom.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()

def validate_rom(path: Path) -> dict[str, int | str]:
    try:
        size = path.stat().st_size
        with path.open("rb") as rom:
            header = parse_header(rom.read(0x40))
        digest = sha256(path)
    except OSError as error:
        raise RomValidationError(f"cannot read ROM: {error}") from error

    expected = {
        "magic": EXPECTED_MAGIC,
        "entrypoint": EXPECTED_ENTRYPOINT,
        "crc1": EXPECTED_CRC1,
        "crc2": EXPECTED_CRC2,
        "title": EXPECTED_TITLE,
        "game_code": EXPECTED_GAME_CODE,
        "revision": EXPECTED_REVISION,
    }
    errors = []
    if size != EXPECTED_SIZE:
        errors.append(f"size 0x{size:X}, expected 0x{EXPECTED_SIZE:X}")
    if digest != EXPECTED_SHA256:
        errors.append(f"SHA-256 {digest}, expected {EXPECTED_SHA256}")
    errors.extend(
        f"{field} {header[field]!r}, expected {value!r}"
        for field, value in expected.items()
        if header[field] != value
    )
    if errors:
        raise RomValidationError("; ".join(errors))

    return {"size": size, "sha256": digest, **header}

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    args = parser.parse_args()
    try:
        info = validate_rom(args.rom)
    except RomValidationError as error:
        parser.exit(1, f"Unsupported ROM: {error}\n")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
