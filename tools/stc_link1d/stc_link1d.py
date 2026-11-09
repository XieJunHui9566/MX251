"""Independent experimental STC-Link1D downloader. Run --help for commands."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys
import time

from protocol import Firmware, Link1D, POST_SESSION_REPORT


def plan(firmware):
    return {"model": "AI8051U-34K64", "transport": "STC-Link1D 34BF:1005",
            "hexSha256": firmware.sha256, "imageSha256": hashlib.sha256(firmware.code).hexdigest(),
            "imageBytes": len(firmware.code), "populatedBytes": firmware.populated_bytes,
            "baseAddress": "FF0000", "ircHz": 40000000,
            "hardwareOptions": "captured 2026-11-09 profile; includes FC->FE option change",
            "eraseScope": "target code erase command; EEPROM preservation is not established",
            "flashReadback": False, "hardwareOpened": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("list", help="Enumerate HID metadata without sending protocol commands")
    for name in ("plan", "flash"):
        p = commands.add_parser(name)
        p.add_argument("--hex", type=Path, required=True)
        if name == "flash":
            p.add_argument("--sha256", required=True, help="Expected HEX file SHA-256 from plan")
            p.add_argument("--device-path", required=True, help="Explicit path from list")
            p.add_argument("--output", type=Path, required=True, help="New evidence directory")
    args = parser.parse_args()
    if args.command == "list":
        from winhid import devices
        print(json.dumps(devices(), indent=2, ensure_ascii=False))
        return 0
    firmware = Firmware.load(args.hex)
    prepared = plan(firmware)
    if args.command == "plan":
        print(json.dumps(prepared, indent=2, ensure_ascii=False))
        return 0
    if args.sha256.lower() != firmware.sha256:
        raise ValueError("HEX SHA-256 differs from approved plan; no hardware opened")
    from winhid import Transport, ensure_official_closed
    ensure_official_closed()
    # Keep the exact already-parsed image immutable for this session.
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / "plan.json").write_text(json.dumps(prepared, indent=2), encoding="utf-8")
    outcome = {"status": "failed", "programAcknowledged": False, "flashReadback": False}
    with (args.output / "trace.ndjson").open("x", encoding="utf-8", buffering=1) as log:
        def record(event):
            row = dict(event, utc=datetime.now(timezone.utc).isoformat(), monotonicNs=time.monotonic_ns())
            log.write(json.dumps(row, ensure_ascii=False) + "\n")

        def progress(event):
            record(event)
            if event.get("stage") != "program":
                print(json.dumps(event, ensure_ascii=False), flush=True)

        session = None
        try:
            with Transport(args.device_path, record) as transport:
                record({"kind": "session", "devicePath": args.device_path, "hexSha256": firmware.sha256})
                session = Link1D(transport, progress)
                outcome.update(session.flash(firmware.code))
                time.sleep(.050)
                transport.reopen()
                transport.write(POST_SESSION_REPORT)
                outcome.update(status="complete", postSessionControlSent=True, hexSha256=firmware.sha256)
        except BaseException as error:
            outcome.update(error=str(error), eraseAttempted=bool(session and session.erase_attempted),
                           configurationAttempted=bool(session and session.options_attempted))
            record({"kind": "failure", **outcome})
            raise
        finally:
            (args.output / "result.json").write_text(json.dumps(outcome, indent=2), encoding="utf-8")
    print(json.dumps(outcome, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, UnicodeError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
