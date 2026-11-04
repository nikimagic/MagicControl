#!/usr/bin/env python3
"""Build an offline D1D1 mode-only voltage-derating image."""

import argparse
import hashlib
from pathlib import Path

EXPECTED_SIZE = 0x40000
EXPECTED_SHA256 = "34c050d30772da07ef262fc7016e0677b9b1b4cdcd90cf43d93f0f15bf6a38c2"

# D303 is the first instruction of the stress/cycle/auxiliary tier selector.
# D300 remains the original low-base protection jump to D3EC.
OFFSET = 0x1D303
EXPECTED = bytes.fromhex(
    "12 e7 c9 94 3d 50 1b c3 90 0a 4f e0 94 26 90 0a 4e "
    "e0 94 02 50 0c 90 0a 54 e0 ff"
)
REPLACEMENT = bytes.fromhex(
    "90 07 a6 e0 54 30 "
    "b4 20 05 7b c8 02 d3 ec "
    "b4 10 05 7b 64 02 d3 ec "
    "7b 00 02 d3 ec"
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        raise SystemExit("refusing to overwrite source image")
    source = args.source.read_bytes()
    digest = hashlib.sha256(source).hexdigest()
    if len(source) != EXPECTED_SIZE or digest != EXPECTED_SHA256:
        raise SystemExit(f"refusing unknown image: size=0x{len(source):X}, sha256={digest}")
    if len(REPLACEMENT) != len(EXPECTED):
        raise SystemExit("internal patch length mismatch")
    patched = bytearray(source)
    actual = bytes(patched[OFFSET:OFFSET + len(EXPECTED)])
    if actual != EXPECTED:
        raise SystemExit(f"original bytes differ at 0x{OFFSET:05X}: {actual.hex(' ')}")
    patched[OFFSET:OFFSET + len(REPLACEMENT)] = REPLACEMENT
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(patched)
    print(f"source sha256:  {digest}")
    print(f"patched sha256: {hashlib.sha256(patched).hexdigest()}")
    print(f"changed range:  0x{OFFSET:05X}-0x{OFFSET + len(REPLACEMENT) - 1:05X}")
    print(f"wrote: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
