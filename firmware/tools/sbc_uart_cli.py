#!/usr/bin/env python3
import argparse
import binascii
import struct
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:
    serial = None

SOF = b"\xA5\x5A"
VERSION = 0x01
MAX_PAYLOAD = 512

MSG_COMMAND = 0x01
MSG_RESPONSE = 0x02
MSG_TELEMETRY = 0x03
MSG_ERROR = 0x04
MSG_OTA_START = 0x10
MSG_OTA_BLOCK = 0x11
MSG_OTA_END = 0x12
MSG_OTA_ABORT = 0x13
MSG_PING = 0x7E
MSG_PONG = 0x7F

DEV_SYSTEM = 0x00
DEV_PROJECTOR = 0x01
DEV_LIGHT = 0x02
DEV_CURTAIN = 0x03
DEV_SCREEN = 0x04
DEV_AC_IR = 0x05
DEV_OTA = 0x06
DEV_RELAY_RAW = 0x07

CMD = {
    "version": (DEV_SYSTEM, 0x01),
    "status": (DEV_SYSTEM, 0x02),
    "reset": (DEV_SYSTEM, 0x03),
    "projector_on": (DEV_PROJECTOR, 0x01),
    "projector_off": (DEV_PROJECTOR, 0x02),
    "projector_status": (DEV_PROJECTOR, 0x03),
    "projector_hdmi": (DEV_PROJECTOR, 0x04),
    "projector_vga": (DEV_PROJECTOR, 0x05),
    "projector_mute_on": (DEV_PROJECTOR, 0x06),
    "projector_mute_off": (DEV_PROJECTOR, 0x07),
    "light_on": (DEV_LIGHT, 0x01),
    "light_off": (DEV_LIGHT, 0x02),
    "light_toggle": (DEV_LIGHT, 0x03),
    "curtain_open": (DEV_CURTAIN, 0x01),
    "curtain_close": (DEV_CURTAIN, 0x02),
    "curtain_stop": (DEV_CURTAIN, 0x03),
    "screen_up": (DEV_SCREEN, 0x01),
    "screen_down": (DEV_SCREEN, 0x02),
    "screen_stop": (DEV_SCREEN, 0x03),
    "ac_on": (DEV_AC_IR, 0x01),
    "ac_off": (DEV_AC_IR, 0x02),
    "ac_temp": (DEV_AC_IR, 0x03),
}


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def build_frame(seq: int, msg_type: int, device: int, command: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too large")
    header = struct.pack("<BBBBBH", VERSION, seq & 0xFF, msg_type, device, command, len(payload))
    crc = crc16_ccitt_false(header + payload)
    return SOF + header + payload + struct.pack("<H", crc)


def read_frame(port, timeout: float = 2.0):
    deadline = time.monotonic() + timeout
    buf = bytearray()
    while time.monotonic() < deadline:
        b = port.read(1)
        if not b:
            continue
        buf += b
        if len(buf) >= 2 and bytes(buf[-2:]) == SOF:
            buf = bytearray(SOF)
            break
    else:
        return None

    fixed = port.read(7)
    if len(fixed) != 7:
        return None
    version, seq, msg_type, device, command, payload_len = struct.unpack("<BBBBBH", fixed)
    payload = port.read(payload_len)
    crc_bytes = port.read(2)
    if len(payload) != payload_len or len(crc_bytes) != 2:
        return None
    rx_crc = struct.unpack("<H", crc_bytes)[0]
    calc_crc = crc16_ccitt_false(fixed + payload)
    return {
        "version": version,
        "seq": seq,
        "msg_type": msg_type,
        "device": device,
        "command": command,
        "payload": payload,
        "crc_ok": rx_crc == calc_crc,
    }


def print_frame(frame):
    if frame is None:
        print("timeout")
        return
    payload = frame["payload"]
    print(
        f"seq={frame['seq']} type=0x{frame['msg_type']:02X} dev=0x{frame['device']:02X} "
        f"cmd=0x{frame['command']:02X} crc_ok={frame['crc_ok']} payload={payload.hex()}"
    )
    if frame["msg_type"] == MSG_RESPONSE and len(payload) >= 3:
        status = payload[0]
        err = payload[1] | (payload[2] << 8)
        print(f"status=0x{status:02X} error=0x{err:04X} data={payload[3:].hex()}")


def load_spsfw(path: Path):
    data = path.read_bytes()
    if len(data) < 20:
        raise ValueError("invalid .spsfw")
    magic, header_ver, target, _reserved, version, size, checksum = struct.unpack("<4sBBHIII", data[:20])
    if magic != b"SPSF" or header_ver != 1:
        raise ValueError("bad .spsfw header")
    image = data[20:]
    if len(image) != size:
        raise ValueError("size mismatch")
    if (binascii.crc32(image) & 0xFFFFFFFF) != checksum:
        raise ValueError("crc32 mismatch")
    return version, target, checksum, image


def send_and_print(port, frame: bytes, timeout: float, expected_seq: int | None = None):
    deadline = time.monotonic() + timeout

    port.write(frame)
    port.flush()
    while time.monotonic() < deadline:
        frame_rx = read_frame(port, max(0.05, deadline - time.monotonic()))
        if frame_rx is None:
            break
        if frame_rx["msg_type"] == MSG_TELEMETRY:
            continue
        if expected_seq is None or frame_rx["seq"] == (expected_seq & 0xFF):
            print_frame(frame_rx)
            return
    print("timeout")


def main() -> int:
    parser = argparse.ArgumentParser(description="SPS MCU UART CLI")
    parser.add_argument("--port", required=True, help="COMx or /dev/ttyAMA0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    sub = parser.add_subparsers(dest="cmd", required=True)
    for name in CMD:
        sp = sub.add_parser(name)
        if name == "ac_temp":
            sp.add_argument("temp", type=int)
    sub.add_parser("ping")
    raw = sub.add_parser("relay")
    raw.add_argument("channel", type=int)
    raw.add_argument("state", type=int, choices=[0, 1])
    ota = sub.add_parser("ota")
    ota.add_argument("file", type=Path)
    ota.add_argument("--block", type=int, default=192)
    ota.add_argument("--corrupt-block", type=int, help="send one OTA block with a bad block CRC, then abort")
    ota.add_argument("--bad-image-crc", action="store_true", help="send a bad image CRC in OTA_START to test OTA_END rejection")

    args = parser.parse_args()
    if serial is None:
        print("pyserial is required: pip install pyserial", file=sys.stderr)
        return 2

    with serial.Serial(args.port, args.baud, timeout=0.05) as port:
        seq = 1
        if args.cmd == "ping":
            send_and_print(port, build_frame(seq, MSG_PING, DEV_SYSTEM, 0x05), args.timeout, seq)
            return 0
        if args.cmd == "relay":
            payload = struct.pack("<BB", args.channel, args.state)
            send_and_print(port, build_frame(seq, MSG_COMMAND, DEV_RELAY_RAW, 0x01, payload), args.timeout, seq)
            return 0
        if args.cmd == "ota":
            version, target, checksum, image = load_spsfw(args.file)
            if args.bad_image_crc:
                checksum ^= 0xFFFFFFFF
            start_payload = struct.pack("<IIIB", version, len(image), checksum, target)
            send_and_print(port, build_frame(seq, MSG_OTA_START, DEV_OTA, 0x00, start_payload), args.timeout, seq)
            seq = (seq + 1) & 0xFF
            for index, offset in enumerate(range(0, len(image), args.block)):
                chunk = image[offset : offset + args.block]
                block_crc = crc16_ccitt_false(chunk)
                if args.corrupt_block is not None and index == args.corrupt_block:
                    block_crc ^= 0x0001
                    print(f"injected_corrupt_block={index}")
                payload = struct.pack("<IIH", offset, index, block_crc) + chunk
                port.write(build_frame(seq, MSG_OTA_BLOCK, DEV_OTA, 0x00, payload))
                port.flush()
                deadline = time.monotonic() + args.timeout
                while time.monotonic() < deadline:
                    frame_rx = read_frame(port, max(0.05, deadline - time.monotonic()))
                    if frame_rx is None:
                        print("timeout")
                        break
                    if frame_rx["msg_type"] == MSG_TELEMETRY:
                        continue
                    if frame_rx["seq"] == seq:
                        print_frame(frame_rx)
                        break
                if args.corrupt_block is not None and index == args.corrupt_block:
                    seq = (seq + 1) & 0xFF
                    send_and_print(port, build_frame(seq, MSG_OTA_ABORT, DEV_OTA, 0x00), args.timeout, seq)
                    return 0
                seq = (seq + 1) & 0xFF
            send_and_print(port, build_frame(seq, MSG_OTA_END, DEV_OTA, 0x00), args.timeout, seq)
            if args.bad_image_crc:
                seq = (seq + 1) & 0xFF
                send_and_print(port, build_frame(seq, MSG_OTA_ABORT, DEV_OTA, 0x00), args.timeout, seq)
            return 0

        device, command = CMD[args.cmd]
        payload = b""
        if args.cmd == "ac_temp":
            payload = struct.pack("<B", args.temp)
        send_and_print(port, build_frame(seq, MSG_COMMAND, device, command, payload), args.timeout, seq)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
