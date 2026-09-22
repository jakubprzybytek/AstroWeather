#!/usr/bin/env python3
"""Measure the board RTC against this PC's clock, over SWD. See docs/RTC.md.

Reads the RTC registers with STM32_Programmer_CLI in HOTPLUG mode, which
leaves the firmware running, and brackets each read with PC timestamps. The
fastest of a few reads is kept, so the offset is good to a few tens of ms.

Examples:
    python tools/rtc_offset.py
        2026-09-22 15:51:34 offset -1.065 s (+-0.045)
    python tools/rtc_offset.py --baseline "2026-09-22 15:51:34" -1.065 --trim 18400
        adds the drift since the baseline and the trim to set next

The PC clock must itself be right: Windows keeps it synced to internet time.
"""

import argparse
import datetime
import re
import subprocess
import sys

PROGRAMMER = r"C:/Program Files/ST/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe"
RTC_TR, RTC_DR, RTC_SSR, RTC_PRER = 0x40002800, 0x40002804, 0x40002808, 0x40002810
READS = 4


def seconds_of_day(moment):
    return moment.hour * 3600 + moment.minute * 60 + moment.second + moment.microsecond / 1e6


def read_once():
    before = datetime.datetime.now()
    # With shadow registers in use, reading SSR or TR freezes TR and DR until
    # DR is read. So, as the HAL does: DR first to release any freeze left by
    # an earlier read, then SSR (freezing TR to match it), TR, and DR to release.
    reads = []
    for address in (RTC_DR, RTC_SSR, RTC_TR, RTC_DR, RTC_PRER):
        reads += ["-r32", hex(address), "0x4"]
    output = subprocess.run([PROGRAMMER, "-c", "port=SWD", "mode=HOTPLUG"] + reads,
                            capture_output=True, text=True).stdout
    after = datetime.datetime.now()
    words = re.findall(r"0x400028[0-9A-F]{2} : ([0-9A-F]{8})", output)
    if len(words) < 5:
        raise RuntimeError("could not read the RTC over SWD:\n" + output)
    ssr, tr, prer = int(words[1], 16), words[2], int(words[4], 16)
    synch_prediv = prer & 0x7FFF
    rtc = (int(tr[2:4]) * 3600 + int(tr[4:6]) * 60 + int(tr[6:8])
           + (synch_prediv - ssr) / (synch_prediv + 1))
    midpoint = before + (after - before) / 2
    # Offset within the day; wraps are folded back to +-12 h.
    offset = (rtc - seconds_of_day(midpoint) + 43200) % 86400 - 43200
    return (after - before).total_seconds(), offset, midpoint


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--baseline", nargs=2, metavar=("TIME", "OFFSET"),
                        help="an earlier result: 'YYYY-MM-DD HH:MM:SS' and its offset in s")
    parser.add_argument("--trim", type=int, default=None,
                        help="trim in use now, in ppm, to suggest the next one")
    args = parser.parse_args()

    span, offset, when = min(read_once() for _ in range(READS))
    print(f"{when:%Y-%m-%d %H:%M:%S} offset {offset:+.3f} s (+-{span / 2:.3f})")

    if args.baseline:
        start = datetime.datetime.strptime(args.baseline[0], "%Y-%m-%d %H:%M:%S")
        elapsed = (when - start).total_seconds()
        gained = offset - float(args.baseline[1])
        residual_ppm = gained / elapsed * 1e6
        print(f"over {elapsed / 3600:.2f} h the RTC gained {gained:+.3f} s: {residual_ppm:+.1f} ppm, "
              f"{residual_ppm * 86400 / 1e6:+.2f} s/day")
        if args.trim is not None:
            # A clock still gaining means the LSI is faster than the trim assumes.
            suggested = round((1 + args.trim / 1e6) * (1 + residual_ppm / 1e6) * 1e6 - 1e6)
            print(f"suggested: time trim {suggested}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
