#!/usr/bin/env python3
"""Log a display board's timeline sync over SWD, one line per sync applied.

Reads g_displayStats (User/Inc/Stats.hpp) from the running board every
--period seconds with STM32_Programmer_CLI in HOTPLUG mode, which attaches
without resetting it, and prints a line whenever the board has applied a new
sync or has restarted. The field list comes from Stats.hpp and the address
from the ELF, so neither goes stale when the struct changes. The ST-LINK has
to be on the display board; with it on the host this reads the host's RAM.

    python tools/stats_log.py                     # until Ctrl+C
    python tools/stats_log.py --duration 3600 > sync.txt

Columns: PC time, seconds since start, syncs applied, jumps, locked,
error (host minus board at the sync, ms), drift (over the last interval that
measured the rate, ms), the servo's rate (ppm), HSITRIM, HSITRIM steps taken,
frames shown, and the last and longest time the sync's data bytes took (us).
See firmware/Docs/TimelineSync.md, "Measuring".
"""

import argparse
import re
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

PROJECT = Path(__file__).resolve().parent.parent
STATS_HEADER = PROJECT / "User" / "Inc" / "Stats.hpp"
DEFAULT_ELF = PROJECT / "build" / "Debug" / "DisplayController.elf"
PROGRAMMER_DIRS = [r"C:\Program Files\ST\STM32Cube\STM32CubeProgrammer\bin"]


def find_tool(name, extra_dirs=()):
    found = shutil.which(name)
    if found:
        return found
    for directory in extra_dirs:
        candidate = Path(directory) / (name + ".exe")
        if candidate.exists():
            return str(candidate)
    sys.exit(f"{name} not found on PATH; see firmware/Docs/Development.md, Prerequisites")


def struct_fields():
    text = STATS_HEADER.read_text(encoding="utf-8")
    body = re.search(r"struct DisplayControllerStats \{(.*?)\n\};", text, re.S).group(1)
    return re.findall(r"^\s*uint32_t\s+(\w+);", body, re.M)


def stats_address(elf):
    nm = find_tool("arm-none-eabi-nm")
    out = subprocess.run([nm, str(elf)], capture_output=True, text=True).stdout
    match = re.search(r"^([0-9a-fA-F]{8})\s+\w\s+g_displayStats$", out, re.M)
    if not match:
        sys.exit(f"g_displayStats not found in {elf}; build the DisplayController first")
    return int(match.group(1), 16)


def signed(value):
    return struct.unpack("<i", struct.pack("<I", value))[0]


def read_stats(programmer, address, fields):
    out = subprocess.run(
        [programmer, "-c", "port=SWD", "mode=HOTPLUG", "-r32", hex(address), hex(len(fields) * 4)],
        capture_output=True, text=True).stdout
    words = []
    for line in out.splitlines():
        match = re.match(r"\s*0x[0-9A-Fa-f]{8}\s*:\s*((?:[0-9A-Fa-f]{8}\s*)+)$", line)
        if match:
            words += [int(word, 16) for word in match.group(1).split()]
    if len(words) != len(fields):
        return None
    return dict(zip(fields, words))


def plausible(stats):
    # A read that raced a reconnect returns garbage; the trim is 7 bits.
    return stats["hsiTrim"] < 128 and stats["syncLocked"] <= 1


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--elf", default=str(DEFAULT_ELF), help="ELF to find g_displayStats in")
    parser.add_argument("--address", type=lambda s: int(s, 0), help="override the ELF's address")
    parser.add_argument("--period", type=float, default=2.0, help="seconds between reads")
    parser.add_argument("--duration", type=float, help="seconds to log (default: until Ctrl+C)")
    args = parser.parse_args()

    fields = struct_fields()
    address = args.address if args.address is not None else stats_address(args.elf)
    programmer = find_tool("STM32_Programmer_CLI", PROGRAMMER_DIRS)

    print("pc_time  elapsed_s  syncs jumps locked  error_ms  drift_ms  rate_ppm trim hsiChg  "
          "frames  data_us max_us", flush=True)
    start = time.time()
    last = None
    try:
        while args.duration is None or time.time() - start < args.duration:
            stats = read_stats(programmer, address, fields)
            if stats is None or not plausible(stats):
                print(f"{time.strftime('%H:%M:%S')}  read failed", flush=True)
            else:
                new_sync = last is None or stats["syncsApplied"] != last["syncsApplied"]
                restarted = last is not None and stats["refreshFrames"] < last["refreshFrames"]
                if new_sync or restarted:
                    print(f"{time.strftime('%H:%M:%S')} {time.time() - start:9.1f} "
                          f"{stats['syncsApplied']:6d} {stats['syncJumps']:5d} "
                          f"{stats['syncLocked']:6d} {signed(stats['syncErrorMicros']) / 1000:9.2f} "
                          f"{signed(stats['syncDriftMicros']) / 1000:9.2f} "
                          f"{signed(stats['syncRatePpm']):9d} {stats['hsiTrim']:4d} "
                          f"{stats['hsiChanges']:6d} {stats['refreshFrames']:7d} "
                          f"{stats['syncDataMicros']:7d} {stats['syncDataMaxMicros']:6d}",
                          flush=True)
                last = stats
            time.sleep(args.period)
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
