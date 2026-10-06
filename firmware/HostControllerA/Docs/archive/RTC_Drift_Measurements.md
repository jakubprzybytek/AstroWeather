> Archived 2026-10-06. Current state: [RTC.md](../RTC.md).

# RTC Drift Measurements

The measurements behind the first board's trim, from 2026-09-22 to
2026-09-27, moved out of [RTC.md](../RTC.md). The method is described there
under *Measuring the Drift*.

## Measurement history

First board (HostControllerA). The drift is what remained on the trim in use,
and the frequency is what that makes the LSI:

| When | Trim | Result |
| --- | --- | --- |
| 2026-09-22 14:14 → 15:20, 66 min | none | +18 372 ppm, uncertain by about 300 ppm because of the sync delay |
| 2026-09-22 14:14 → 15:49, 95 min | none | **+18 390 … +18 405 ppm**, 32 588.8 Hz; `time trim 18400` set |
| 2026-09-22 15:57 → 16:24, 27 min | +18 400 | +341 ± 60 ppm, 32 599.9 Hz: the fastest seen, on a board running for hours |
| 2026-09-22 16:26 → 16:33 | +18 400 | ended by the power-cycle test |
| 2026-09-22 16:35 → 17:25, 50 min, just after power-up | +18 400 | +1 ± 30 ppm, 32 588.8 Hz |
| 2026-09-22 17:25 → 18:24, 59 min | +18 400 | +142 ± 27 ppm, 32 593.4 Hz: rising as the board runs |
| 2026-09-22 18:24 → 19:54, 1.5 h | +18 400 | +275 ± 18 ppm, 32 597.8 Hz: still climbing |
| 2026-09-22 19:54 → 22:33, 2.7 h | +18 400 | +308 ± 10 ppm, 32 598.8 Hz: near the fastest seen |
| 2026-09-22 16:35 → 22:33, 6.0 h in total | +18 400 | +230 ± 5 ppm, 32 596.3 Hz on average, 19.9 s/day |
| 2026-09-22 22:35 → 22:41 | +18 400 | ended by a power cycle, recovering the USB console port |
| 2026-09-22 22:51:31 → ?, offset +0.043 s | +18 400 | ended by an unplugged board, not read before; the clock was later set by hand, 2.1 s ahead |
| 2026-09-22 23:49:52 → 2026-09-23 09:09, 9.3 h overnight, board up since 23:11 | +18 400 | **+798 ± 3 ppm**, 32 614.8 Hz, 69 s/day: the fastest by far |
| 2026-09-22 23:49:52 → 2026-09-23 09:32, 9.7 h | +18 400 | +806 ± 3 ppm, 32 614.9 Hz; ended by the first API sync, which stepped the RTC back 28.6 s |
| from 2026-09-23 09:40:14, offset +0.010 s | +18 400 | ended by later API syncs; the SWD run could not be kept going through the testing of the following days |
| 2026-09-23 10:16 → 12:53, 2.6 h, from the API syncs | +18 400 | about +324 ppm (stepped back 2.89 s) |
| 2026-09-27 18:10 → 19:31, 81 min, from the API syncs | +18 400 | about **+1300 ppm** (stepped back 6.35 s) |
| 2026-09-27 20:10 → 20:17, 7 min, from the API syncs | +18 400 | about +950 ppm (stepped back 0.40 s; rough, a sync leaves up to 250 ms) |
| from 2026-09-27 20:21 | +19 300 | `time trim 19300` set, centring the +300 … +1300 ppm range seen with +18 400; the day/night swing of about ±500 ppm remains |

From 2026-09-23 the API syncs were the reference: each `Clock sync` line gives
the RTC's error against the server, to about ±150 ms. The SWD tool compares
against the PC's clock instead, which was found 0.7 s behind the server on
2026-09-27 with Windows time not synchronized.

## Observations from the runs

On one trim the drift climbed from +1 ppm just after a power-up to +308 ppm
six hours later, near the +341 ppm measured on a board that had been running
for hours. A following overnight run averaged +798 ppm. The cause was not
found. It builds up over hours, which is too slow for the chip warming up, so
ambient temperature or the supply are the likelier candidates; the fastest run
being the overnight one points at a cooler room. The PC's clock was ruled out:
checked against time.windows.com after the overnight run, it was 0.5 s off,
about 15 ppm over that run.

Three resets and a re-flash each cost less than 5 ms, below what
`tools/rtc_offset.py` can resolve. The first API sync stepped the RTC by
−28.574 s; `tools/rtc_offset.py` then found it within 10 ms of the PC clock.

Under the T02 architecture (until 2026-10-03) the generated SNTP client in
`LWIP/App/sntp.c` also wrote the RTC when it ran, so it was kept off.
