#!/usr/bin/env python3
"""Stream device harness output and report the result.

Two transports:
  --port  Serial device (ESP32/RP2040): resets the board, reads lines.
  --exec  Subprocess (e.g. OpenOCD with semihosting for STM32): spawns the
          command, reads its stdout, and terminates it once done.

Either way, reads lines until the `TAMP-DEVICE-RESULT:` sentinel emitted by
devices/common/tamp_bench.c.

Exit codes:
    0  device reported PASS
    1  device reported FAIL
    2  timed out waiting for the sentinel
"""

import argparse
import re
import shlex
import subprocess
import sys
import threading
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


def read_from_serial(args, handle_line):
    with serial.Serial(args.port, args.baud, timeout=1) as port:
        reset_device(port)
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            raw = port.readline()
            if raw and handle_line(raw):
                return
    return


def read_from_exec(args, handle_line):
    # Running the user-supplied command is the point of --exec.
    proc = subprocess.Popen(shlex.split(args.exec), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)  # noqa: S603
    # OpenOCD blocks in readline() with no per-read timeout, so enforce the
    # overall deadline from a watchdog; killing the process unblocks readline.
    watchdog = threading.Timer(args.timeout, proc.kill)
    watchdog.start()
    try:
        for raw in proc.stdout:
            if handle_line(raw):
                return
    finally:
        watchdog.cancel()
        proc.kill()
        proc.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--port", help="Serial port (e.g. /dev/ttyUSB0).")
    source.add_argument("--exec", help="Command whose stdout carries the device output (e.g. an openocd invocation).")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default 115200).")
    parser.add_argument("--timeout", type=float, default=600.0, help="Seconds to wait for the sentinel (default 600).")
    parser.add_argument("--benchmark", action="store_true", help="Reprint BENCH/INFO lines as a block at the end.")
    args = parser.parse_args()

    bench_lines = []
    result = None

    def handle_line(raw):
        nonlocal result
        line = ANSI_RE.sub("", raw.decode("utf-8", errors="replace")).rstrip("\r\n")
        if not line:
            return False
        print(line, flush=True)
        if line.startswith("BENCH ") or line.startswith("INFO "):
            bench_lines.append(line)
        if SENTINEL in line:
            result = line
            return True
        return False

    if args.port:
        read_from_serial(args, handle_line)
    else:
        read_from_exec(args, handle_line)

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
