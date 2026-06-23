#!/usr/bin/env python3
import argparse
import binascii
import struct
from pathlib import Path

MAGIC = b"SPSF"
HEADER_VERSION = 1


def parse_version(text: str) -> int:
    parts = [int(p, 0) for p in text.split(".")]
    while len(parts) < 4:
        parts.append(0)
    return ((parts[0] & 0xFF) << 24) | ((parts[1] & 0xFF) << 16) | ((parts[2] & 0xFF) << 8) | (parts[3] & 0xFF)


def main() -> int:
    parser = argparse.ArgumentParser(description="Pack SPS MCU app binary into .spsfw OTA image")
    parser.add_argument("input", type=Path, help="app .bin file")
    parser.add_argument("-o", "--output", type=Path, help="output .spsfw path")
    parser.add_argument("--version", default="0.2.0", help="firmware version, e.g. 0.2.0")
    parser.add_argument("--target", choices=["a", "b"], default="b", help="OTA target slot")
    args = parser.parse_args()

    image = args.input.read_bytes()
    checksum = binascii.crc32(image) & 0xFFFFFFFF
    version = parse_version(args.version)
    target = 0 if args.target == "a" else 1
    output = args.output or args.input.with_suffix(".spsfw")
    header = struct.pack("<4sBBHIII", MAGIC, HEADER_VERSION, target, 0, version, len(image), checksum)
    output.write_bytes(header + image)
    print(f"output={output}")
    print(f"version=0x{version:08X}")
    print(f"target_slot={args.target.upper()}")
    print(f"size={len(image)}")
    print(f"crc32=0x{checksum:08X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
