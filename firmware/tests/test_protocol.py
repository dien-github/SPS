import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from sbc_uart_cli import build_frame, crc16_ccitt_false


def test_crc16_check_vector():
    assert crc16_ccitt_false(b"123456789") == 0x29B1


def test_frame_builder_crc_tail():
    frame = build_frame(1, 0x7E, 0, 5, b"")
    assert frame[:2] == b"\xA5\x5A"
    assert frame[2] == 1
    assert len(frame) == 11
