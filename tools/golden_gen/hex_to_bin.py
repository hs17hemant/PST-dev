#!/usr/bin/env python3
"""
hex_to_bin.py — convert MS-PST spec hex dumps (verbatim) into binary fixtures.

Input format (matches Microsoft Learn pages, e.g. §3.4 / §3.6 / §3.7):

    # comments allowed; ignored
    0000000000007000  0F 06 00 00 00 00 00 00-0C 00 00 00 00 00 00 00
    0000000000007010  00 00 00 00 00 00 00 00-00 00 00 00 02 00 00 00
    ...

The first column is the absolute file offset of that row (informational).
The 16 hex bytes that follow are the actual data, optionally split by '-'
between the two 8-byte halves. Trailing ASCII annotations (e.g. *...*) are
ignored. Rows must be contiguous with no gaps.

Usage:
    python hex_to_bin.py INPUT.hex OUTPUT.bin
"""
import sys
import re
from pathlib import Path


def parse_hex_dump(text: str) -> bytes:
    out = bytearray()
    expected_offset = None
    # Offset, then between 1 and 16 hex byte tokens (split by spaces or '-').
    # Some MS-PST spec dumps end on a partial last row (e.g. §3.8 ends with
    # just `EC 00` at offset 0x4900) — we accept short final rows but reject
    # short rows in the middle.
    offset_re = re.compile(r"^\s*([0-9A-Fa-f]{8,16})\s+(.+)$")
    short_row_seen = False

    for raw_line in text.splitlines():
        line = raw_line.split("*", 1)[0]  # strip ASCII annotation
        line = line.split("#", 1)[0]
        line = line.strip()
        if not line:
            continue
        m = offset_re.match(line)
        if not m:
            continue

        offset = int(m.group(1), 16)
        byte_field = m.group(2).replace("-", " ")
        tokens = [t for t in byte_field.split() if t]
        if not tokens or not all(re.fullmatch(r"[0-9A-Fa-f]{2}", t) for t in tokens):
            continue  # not a hex-data row (e.g. metadata)

        if expected_offset is None:
            expected_offset = offset
        elif offset != expected_offset:
            raise ValueError(
                f"discontiguous offset 0x{offset:X}, expected 0x{expected_offset:X}"
            )

        if short_row_seen:
            raise ValueError(
                f"row after a short row at 0x{offset:X} — short rows allowed only at end"
            )

        bytes_in_row = [int(t, 16) for t in tokens]
        if len(bytes_in_row) > 16:
            raise ValueError(f"row at 0x{offset:X} has {len(bytes_in_row)} bytes, max 16")
        if len(bytes_in_row) < 16:
            short_row_seen = True
        out.extend(bytes_in_row)
        expected_offset += len(bytes_in_row)

    return bytes(out)


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    in_path = Path(sys.argv[1])
    out_path = Path(sys.argv[2])
    text = in_path.read_text(encoding="utf-8")
    data = parse_hex_dump(text)
    out_path.write_bytes(data)
    print(f"{in_path.name} -> {out_path.name}: {len(data)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
