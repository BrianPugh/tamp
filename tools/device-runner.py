#!/usr/bin/env python3
"""Reset an ESP32 over serial, stream its output, and report the harness result.

Reads lines from the device until the `TAMP-DEVICE-RESULT:` sentinel emitted by
devices/espidf/main/main.c.

Exit codes:
    0  device reported PASS
    1  device reported FAIL
    2  timed out waiting for the sentinel
"""

import argparse
import re
import sys
import time

import serial

SENTINEL = "TAMP-DEVICE-RESULT:"
ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


def reset_device(port):
    """Perform the esptool reset dance to boot into the normal application.

    A no-op on boards without DTR/RTS reset wiring (e.g. RP2040 USB-CDC),
    where the harness's run-forever loop provides a fresh run instead.
    """
    # Assert RTS (EN low) with DTR low to reset into normal boot, then release.
    port.setDTR(False)
    port.setRTS(True)
    time.sleep(0.1)
    port.setRTS(False)
    time.sleep(0.2)
    # Drop any output buffered from before the reset so a stale sentinel from a
    # previous run can't be mistaken for this run's result.
    port.reset_input_buffer()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", required=True, help="Serial port (e.g. /dev/ttyUSB0).")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default 115200).")
    parser.add_argument("--timeout", type=float, default=600.0, help="Seconds to wait for the sentinel (default 600).")
    parser.add_argument("--benchmark", action="store_true", help="Reprint BENCH/INFO lines as a block at the end.")
    args = parser.parse_args()

    bench_lines = []
    result = None

    with serial.Serial(args.port, args.baud, timeout=1) as port:
        reset_device(port)
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            raw = port.readline()
            if not raw:
                continue
            line = ANSI_RE.sub("", raw.decode("utf-8", errors="replace")).rstrip("\r\n")
            if not line:
                continue
            print(line, flush=True)
            if line.startswith("BENCH ") or line.startswith("INFO "):
                bench_lines.append(line)
            if SENTINEL in line:
                result = line
                break

    if result is None:
        print(f"TIMEOUT: no result within {args.timeout}s", file=sys.stderr)
        return 2

    if args.benchmark and bench_lines:
        print("\n--- Benchmark summary ---")
        for line in bench_lines:
            print(line)

    return 0 if "PASS" in result.split(SENTINEL, 1)[1] else 1


if __name__ == "__main__":
    sys.exit(main())
