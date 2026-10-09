# The Host on HSI16, October 2026

> Archived 2026-10-09. Current state: [TimelineSync.md](../TimelineSync.md#clocks-and-hsitrim).

Until build 2 of the HostController the host clocked everything from its
HSI16 RC oscillator, trimmed from the console with `time hsi`, saved in the
EEPROM and applied at boot. It now runs from a 24 MHz crystal through the PLL,
and its HSI16 clocks nothing. These are the measurements from the HSI16
period, kept for the numbers behind the timeline sync's design.

## Trimming the host's HSI

`tools/hsi_measure.py` reads the trim with `time hsi`, turns on `stats on`,
fits a line to the PC's arrival times of the reports the host sends every
5000 ms of its own clock, and prints the host's clock against the PC's and the
trim to set. Measured on 2026-10-05 against the PC at trim 64: the host
+2660 ppm (trim 63 estimated at about -650 ppm), the display board at `0x11`
-2147 to -2500 ppm.

## How steady the host's HSI16 was

Against the PC's clock, over 2-minute windows on 2026-10-07 the host's HSI16
read +3052 to +3251 ppm with nothing in its log at those times; in 5-minute
windows +2639 to +3252 ppm. The display board's clock against the host's
moved by the same amounts the other way, a few ppm to 300 ppm between one
interval's average and the next. The cause of the host's movement was never
found; its supply and load were the first suspects.

## Accuracy with both boards on HSI16

Board `0x11`, 14 regular 2-minute syncs from 15:10 on 2026-10-07: +7.3, -9.5,
+15.6, -16.5, +6.9, +16.3, +3.6, -0.5, -22.4, +6.1, +1.0, +0.1, -11.3 and
+9.6 ms, nine of them within 10 ms; locked throughout, no burst, no HSITRIM
step. The larger ones coincided with the host's HSI16 moving 100-200 ppm
within an interval. With 5-minute syncs the same bench found 30-50 ms, and
90 ms when the host's clock stepped, which is why the interval is 2 minutes.
The full bench log is in
[Timeline_Sync_Bench_2026-10-07.md](Timeline_Sync_Bench_2026-10-07.md).
