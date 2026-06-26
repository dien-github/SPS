#!/usr/bin/env python3
import argparse
import binascii
from pathlib import Path


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def main() -> int:
    parser = argparse.ArgumentParser(description="CRC helper for SPS protocol/OTA")
    parser.add_argument("file", type=Path)
    args = parser.parse_args()
    data = args.file.read_bytes()
    print(f"crc16_ccitt_false=0x{crc16_ccitt_false(data):04X}")
    print(f"crc32_ieee=0x{binascii.crc32(data) & 0xFFFFFFFF:08X}")
    print(f"size={len(data)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
