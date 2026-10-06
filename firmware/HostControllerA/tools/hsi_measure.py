#!/usr/bin/env python3
"""Measure the host's HSI16 clock against the PC and suggest a 'time hsi' trim.

'stats on' makes the host print a report every 5000 ms of its own clock, on
an absolute schedule; the PC's arrival times of those reports, fitted to a
line, give the host's clock rate. Ten minutes gives a few tens of ppm, far
finer than a trim step (about 0.33 %, 3300 ppm). See firmware/Docs/Display.md,
"Trimming the host's HSI".

The PC's own clock is the reference: its error is tens of ppm at most, which
does not change the step chosen. Requires pyserial.

    python tools/hsi_measure.py                 # 10 minutes
    python tools/hsi_measure.py --duration 300
"""

import argparse
import re
import sys
import threading
import time

import serial

from astro_console import DEFAULT_BAUD, resolve_port

STEP_PPM = 3300.0  # one HSITRIM step on a G0, measured on a G070 2026-10-05
REPORT_PERIOD_S = 5.0


def fit(xs, ys):
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    slope = sxy / sxx
    worst = max(abs(y - (my + slope * (x - mx))) for x, y in zip(xs, ys))
    return slope, worst


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", help="serial port (default: auto-detect)")
    parser.add_argument("--duration", type=float, default=600.0, help="seconds to measure")
    args = parser.parse_args()

    conn = serial.Serial(resolve_port(args.port), DEFAULT_BAUD, timeout=0.05)
    lines = []  # (pc seconds, text)
    stop = threading.Event()

    def reader():
        buffer = b""
        while not stop.is_set():
            data = conn.read(4096)
            now = time.perf_counter()
            buffer += data
            while b"\n" in buffer:
                line, buffer = buffer.split(b"\n", 1)
                lines.append((now, line.decode(errors="replace").strip()))

    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    time.sleep(1.0)
    conn.write(b"time hsi\r\n")
    time.sleep(1.0)
    conn.write(b"stats on\r\n")
    start = time.perf_counter()
    while time.perf_counter() - start < args.duration:
        remaining = args.duration - (time.perf_counter() - start)
        print(f"\rmeasuring, {remaining:4.0f} s left ", end="", file=sys.stderr)
        time.sleep(1.0)
    print(file=sys.stderr)
    conn.write(b"stats off\r\n")
    time.sleep(1.0)
    stop.set()
    thread.join(timeout=1.0)
    conn.close()

    trim = None
    for _, text in lines:
        match = re.search(r"OK time-hsi=(\d+)", text)
        if match:
            trim = int(match.group(1))
    # The first report comes at once on 'stats on'; the rest every 5000 host ms.
    reports = [t for t, text in lines if "[STATS]" in text][1:]
    if len(reports) < 10:
        print(f"only {len(reports)} periodic reports; measure longer", file=sys.stderr)
        return 1

    host_seconds = [i * REPORT_PERIOD_S for i in range(len(reports))]
    slope, worst = fit(host_seconds, reports)  # PC seconds per host second
    ppm = (1.0 / slope - 1.0) * 1e6
    span = reports[-1] - reports[0]
    print(f"host clock {ppm:+.0f} ppm against the PC, from {len(reports)} reports over "
          f"{span:.0f} s (worst arrival {worst * 1000:.0f} ms off the line)")
    if trim is None:
        print("no 'OK time-hsi=' reply: firmware without 'time hsi'?")
        return 1
    steps = -round(ppm / STEP_PPM)
    if steps == 0:
        print(f"trim {trim} is already the closest; no change")
    else:
        print(f"trim {trim} -> {trim + steps}: 'time hsi {trim + steps}', "
              f"about {ppm + steps * STEP_PPM:+.0f} ppm after; measure again to confirm")
    return 0


if __name__ == "__main__":
    sys.exit(main())
