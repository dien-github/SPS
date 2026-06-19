#!/usr/bin/env python3
"""Send a demo RFID UID to svcAuthentication over D-Bus.

Run this from the same shell/session as run_wsl_demo.sh, or export the
DBUS_SYSTEM_BUS_ADDRESS printed by that script.

Fallback command:
  qdbus --system com.sps.auth /com/sps/auth com.sps.auth.UnlockScreen RFID001
"""

from __future__ import annotations

import argparse
import sys


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Fake an RFID swipe for SPS demo")
    parser.add_argument(
        "--uid",
        default="RFID001",
        help="RFID UID to send to com.sps.auth.UnlockScreen (default: RFID001)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        import pydbus
    except ImportError:
        print("pydbus is not installed. Install tests requirements first:")
        print("  python3 -m pip install -r tests/requirements.txt")
        print("Fallback with qdbus:")
        print(f"  qdbus --system com.sps.auth /com/sps/auth com.sps.auth.UnlockScreen {args.uid}")
        return 2

    try:
        bus = pydbus.SystemBus()
        auth = bus.get("com.sps.auth", "/com/sps/auth")
        ok = auth.UnlockScreen(args.uid)
        print(f"UnlockScreen({args.uid}) -> {ok}")

        try:
            lecturer = auth.GetAuthenticatedLecturer()
            if lecturer:
                print(f"Authenticated lecturer: {lecturer}")
        except Exception:
            pass

        return 0 if ok else 1
    except Exception as exc:
        print(f"Failed to call svcAuthentication over D-Bus: {exc}", file=sys.stderr)
        print("Fallback with qdbus:", file=sys.stderr)
        print(
            f"  qdbus --system com.sps.auth /com/sps/auth com.sps.auth.UnlockScreen {args.uid}",
            file=sys.stderr,
        )
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
