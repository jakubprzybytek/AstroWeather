# RTC and Clock

## Overview

The HostController keeps the date and time in the STM32G0B1 RTC and shows the
time as `HH:MM` on numeric display 3 of the local board. The date is tracked
but not displayed. The RTC runs from the
internal LSI oscillator, which is only accurate to a few percent, so each board
is **trimmed**: its LSI error is measured once and stored in the EEPROM, and the
RTC prescalers are set to cancel it.

Every successful astro fetch compares the RTC with the time the server sends
and steps it when it is a second or more out; it also logs the drift measured
across fetches and what that says about the trim. See
[Sync from the API](#sync-from-the-api). `time set` sets it by hand. The time
is kept over a reset or flashing. After a power loss display 3 shows `--:--`
until the next fetch or `time set`. See [Reset and power loss](#reset-and-power-loss).

Only the HostController firmware has the clock. The DisplayController variant
builds the same `MX_RTC_Init()` but does not use the RTC.

## Clock Source

The G0B1 RTC can be clocked from only three sources:

| Source | Status on this board |
| --- | --- |
| LSE, a 32.768 kHz crystal on PC14/PC15 | Not fitted. PC14/PC15 are unused. |
| HSE / 32 | No HSE is fitted. |
| LSI, the internal RC oscillator, nominally 32 kHz | **Used.** |

HSI16 cannot clock the RTC.

The LSI needs no parts, but it is not precise. The datasheet allows a wide
spread between parts, and it moves with temperature and supply voltage. It also
has no trim register: `RCC_CSR` only turns it on and reports it ready. The
correction therefore happens in the RTC, not the oscillator.

The first board's LSI runs **about 1.85% fast, between 32 589 and 32 615 Hz**
against a nominal 32 000 Hz, well inside the datasheet spread. Untrimmed, the
clock gained 66 s an hour, or 26 minutes a day. The 26 Hz is not scatter in the
measurement: the frequency really moves, over hours. Each 1 Hz is about 30 ppm,
or 2.6 s a day, so the trim needs the frequency to roughly 0.1 Hz to be worth
much, and no fixed trim can beat the spread, about ±34 s a day here. See the
[measurement history](#measurement-history).

## CubeMX Configuration

In `HostControllerA.ioc`:

- **Timers → RTC**: *Activate Clock Source* and *Activate Calendar* ticked.
  Alarms, wake-up, tamper and the outputs are off.
- **Clock Configuration**: the RTC clock mux is set to **LSI**, which also
  switches the LSI on in `SystemClock_Config()`.
- **RTC → Parameter Settings**: 24-hour format, binary data format, asynchronous
  prescaler 127, synchronous prescaler **249**, so 32 000 / 128 / 250 = 1 Hz
  for a nominal LSI. The default of 255 assumes a 32 768 Hz LSE.

The generated `MX_RTC_Init()` sets the time to 00:00:00, 1 September of year
0, on every boot. The `USER CODE BEGIN Check_RTC_BKUP` section in `main.c` returns
before that when the time has been set; `RTC_TIME_SET_MARKER` in `main.h`
defines the marker. That is the only change to generated code; see
[CubeMXCompliance.md](CubeMXCompliance.md) and the next section.

## Software

| File | Responsibility |
| --- | --- |
| `User/Inc/HostController/ClockTask.hpp`, `User/Src/HostController/ClockTask.cpp` | Draws the time, sets the time and trim, and owns RTC access. |
| `User/Inc/HostController/RtcTrim.hpp` | Pure maths: trim in ppm → prescalers and calibration. Tested by `tests/RtcTrimTests.cpp`. |
| `User/Inc/HostController/CalendarDate.hpp` | Pure maths: leap years, month lengths, the weekday the RTC needs, and dates to seconds and back. Tested by `tests/CalendarDateTests.cpp`. |
| `User/Inc/HostController/ClockSync.hpp` | Pure maths for the API sync: the constants, and the drift tracker. Tested by `tests/ClockSyncTests.cpp`. |
| `User/Src/HostController/AstroDataParser.cpp` | Parses the payload's `time` record. |
| `User/Src/HostController/AstroDataRefreshTask.cpp` | Calls `ClockTask::syncToServer()` after each successful parse. |
| `User/Src/Console/TimeCommand.cpp` | The `time` console commands. |
| `User/Src/HostController/AppVariant.cpp` | Applies the stored display setting and trim, then starts the task. |
| `tools/rtc_offset.py` | Measures the RTC against the PC clock over SWD. |

`ClockTask` redraws only when the minute changes. It sleeps until the next
minute boundary, which it works out from the RTC seconds. `time set`, `time display`
and a trim change wake it at once. The task's timer runs on the HSI and the RTC
on the LSI, so it may wake a little early. It then sees the same minute and
sleeps for the remainder. A new minute can appear up to about 2 s late.

Display 3 is shared. An astro refresh writes all four numeric displays, so its
value shows until the next minute, when the time comes back.

All RTC access goes through `ClockTask`, under one mutex. Reads call
`HAL_RTC_GetTime()` and then `HAL_RTC_GetDate()`. Reading the time freezes the
calendar shadow registers until the date register is read, so a missing
`GetDate()` would make the clock appear to stop.

## Reset and Power Loss

The RTC, its calibration register and the five backup registers are in the
backup domain. A reset does not clear them, but losing power does. The two
cases are told apart by a marker, `"TIME"` (`0x54494D45`), which
`ClockTask::setTime()` writes to backup register 0 after setting the time.

| Event | Backup domain | Result |
| --- | --- | --- |
| Reset or flashing | kept, marker present | The time carries on. `MX_RTC_Init()` returns before setting 00:00. |
| Power-up | cleared, no marker | 00:00 is set; display 3 shows `--:--` and `time show` reports `set=no` until the next astro fetch or `time set`. |

Two other boot steps would otherwise disturb a running clock, because
rewriting the prescalers drops the part of the current second already counted:

- `HAL_RTC_Init()` rewrites them unless the calendar counts as initialised
  (`RTC_ICSR.INITS`), which the hardware sets only for a non-zero year. The
  date that `setDateTime()` writes covers this, as the RTC's two-digit year
  holds 2000..2099 and is never 0.
- `setTrim()` at boot compares the new prescalers with `RTC_PRER`, which still
  holds the trim from before the reset, not with the generated 127/249. It
  rewrites them only if they differ.

Measured on the first board, three resets and a re-flash each cost less than
5 ms, below what `tools/rtc_offset.py` can resolve. The RTC keeps counting
through a reset.

The LSI runs from VDD, so the clock cannot keep time while unpowered even if a
battery were on VBAT: the registers would survive but not count, and the time
would resume from the moment of the outage. Keeping time through a power cut
needs a 32.768 kHz crystal (LSE) as well as a battery.

## Console Commands

HostController only. See also `help time`.

| Command | Effect |
| --- | --- |
| `time show` | `OK time=2026-09-22 20:15:03.123 set=yes trim=+18400ppm prediv=3/8146 calm=26`: date and time to the millisecond, whether it has been set since power-up, and the trim with the registers it produced. |
| `time set <YYYY-MM-DD> <HH:MM[:SS]>` | Set the date and time, 24-hour; seconds optional, 00 if left out. Kept over a reset; lost on power loss. |
| `time trim <ppm>` | Apply and save the trim. `0` removes it. Valid range ±100 000. |
| `time display on\|off` | Show or blank display 3. Saved. |

`settings show` and `status` include the stored trim and display setting.

## Trimming

The trim is the LSI's error from 32 000 Hz, in ppm. It is positive when the LSI
is fast, which makes an untrimmed clock gain time. The trim is stored as the
`ClockTrim` settings record (tag `0x02`, see [Settings.md](Settings.md)) and
applied at every boot.

The RTC makes its 1 Hz tick by dividing its clock by
`(PREDIV_A + 1) × (PREDIV_S + 1)`. It can also discard `CALM` clock cycles out
of every 2²⁰, about 32 s, which slows it by roughly 0.95 ppm per cycle.
`RtcTrim::compute()` uses both:

1. Work out the trimmed LSI frequency, `F = 32 000 Hz × (1 + ppm / 10⁶)`.
2. Fix `PREDIV_A` at 3. With 127, each step of `PREDIV_S` would be about
   3 900 ppm, far more than the calibration can make up. With 3 it is about
   120 ppm.
3. Choose `PREDIV_S + 1 = floor(F / 4)`. Rounding down keeps the divided clock
   at 1 Hz or faster, so the calibration only ever removes cycles and the
   `CALP` bit is never needed.
4. Remove the remainder with `CALM = round(2²⁰ × (F − P) / P)`, where
   `P = 4 × (PREDIV_S + 1)`. The calibrated clock is `F × 2²⁰ / (2²⁰ + CALM)`,
   which then equals `P`.

Across the whole ±100 000 ppm range the tick is then within 1 ppm (0.09 s a day)
of 1 Hz, assuming the trim is correct. For +18 400 ppm this gives
`PREDIV_A = 3`, `PREDIV_S = 8146` and `CALM = 26`. Read back over SWD, that is
`RTC_PRER = 0x00031FD2` and `RTC_CALR = 0x0000001A`.

The prescalers can only be written in initialisation mode, which keeps the
time but drops the part of the current second already counted, up to a second.
A test measured 0.87 s lost each time. So `setTrim()` rewrites the prescalers
only when they change, which a small adjustment usually does not.
`HAL_RTCEx_SetSmoothCalib()` sets `CALM` with a 32 s period and no added
pulses, without initialisation mode. Re-applying a trim, or moving it
10 ppm, measured no loss beyond the 5 ms read noise.

The lower `PREDIV_A` makes the RTC draw slightly more current, which does not
matter for a mains-powered controller.

What limits accuracy is how stable the LSI stays, not the arithmetic or the
trim. On the first board the drift on one trim climbed from +1 ppm just after a
power-up to +308 ppm six hours later, near the +341 ppm measured on a board
that had been running for hours. A following overnight run averaged +798 ppm,
so the spread seen so far is about 800 ppm, or 69 s a day. The cause is not
known. It builds up over hours, which is too slow for the chip warming up, so
ambient temperature or the supply are the likelier candidates; the fastest run
being the overnight one points at a cooler room. The PC's clock is ruled out:
checked against time.windows.com after the overnight run, it was 0.5 s off,
about 15 ppm over that run. The MCU's own temperature sensor would settle it; the ADC
already reads it, but the two have not been logged together.

A trim is therefore worth taking from a settled period rather than from a run
that is still climbing, or aimed at the middle of the range seen over a whole
day and night, and the residual spread stays. Resyncing from the API
bounds the error regardless, which matters more than refining the trim.

## Measuring the Drift

The PC clock is the reference; Windows keeps it synced to internet time.
`tools/rtc_offset.py` reads the RTC over SWD in HOTPLUG mode, which leaves the
firmware running. It stamps the PC time before and after each read and keeps the
fastest of four reads, so one reading is good to a few tens of ms.

1. Set a trim, or 0 to measure the raw LSI: `time trim <ppm>`.
2. Set the time with `time set`, aiming at a whole second. Sending it over an
   already-open port, rather than starting a tool that then connects, keeps the
   delivery delay to a few tens of ms.
3. Record a baseline: `python tools/rtc_offset.py`, which prints for example
   `2026-09-22 22:51:31 offset +0.043 s (+-0.051)`. The baseline absorbs the
   `time set` delivery delay, about 50 ms, so it does not count as drift.
4. Hours later, run it with that baseline and the trim in use:

   ```bash
   python tools/rtc_offset.py --baseline "2026-09-22 22:51:31" 0.043 --trim 18400
   ```

   It prints the drift in ppm and s/day, and the `time trim` to set next:
   `(1 + trim) × (1 + drift) − 1`.

The error in the result is about 0.1 s divided by the elapsed time. That is
28 ppm after one hour and 3 ppm after ten. A reset or flashing keeps the
baseline valid; a power cycle, a `time set`, or a `time trim` that changes the
prescalers does not, and needs a new baseline. Nor does an astro fetch that
steps the RTC: the log gives the step (`Clock sync: RTC stepped by -28.574 s`),
which can be taken off the baseline offset, but it is good only to about
±150 ms, so a fresh baseline is better. With the RTC stepped once it is 250 ms
out, the SWD method now suits measuring between fetches, or with none running.

**Read order matters.** With shadow registers in use, reading `RTC_SSR` or
`RTC_TR` freezes `RTC_TR` and `RTC_DR` until `RTC_DR` is read. A block read
that starts at `RTC_TR` ends on `SSR` and leaves them frozen. The next read then
gets an old `TR` with a current `SSR`, which can be off by a whole second or
more. It also hands the firmware one stale reading. The tool reads `DR` first
to release any freeze, then `SSR`, `TR` and `DR`, the same order as the HAL.

### Measurement history

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
| from 2026-09-23 09:40:14, offset +0.010 s | +18 400 | running; after the step, the RTC was within 10 ms of the PC |

## Limits

- **The time is lost when power is lost**, until the next astro fetch or
  `time set`. See [Reset and power loss](#reset-and-power-loss).
- **No time zone.** The RTC holds local time. The server sends the
  configuration's local time with daylight saving applied, so a DST change
  reaches the clock at the next fetch after it.
- **Synced only as often as something fetches.** Scheduled refreshes do not
  exist yet (`RefreshTrigger::Scheduled` is never raised), so the clock is
  corrected by switch 1, `astro refresh` and `wifi test`.
- **The drift measurement is in RAM.** A reset or flashing restarts it, though
  the time itself survives.
- **The date is tracked but never shown.** It is kept so that an API sync and a
  future date display have it. The RTC rolls it over, including leap years. Its
  two-digit year limits it to 2000..2099.
- **LSI stability.** The trim is right only for the conditions it was measured
  in. The LSI has been seen to move by about 800 ppm between runs; see
  [Trimming](#trimming).

## Sync from the API

The astro API's payload carries a `time` header record, the configuration's
local time when the server rendered the response, as
`YYYY-MM-DDTHH:MM:SS.mmm` with daylight saving applied; see
`sst/docs/api-payload.md`. Servers before 2026-09-23 sent whole seconds,
truncated, without `.mmm`; the firmware accepts both, and takes the precision
of each comparison from the form it gets (`ClockSync::Precision`). Each successful fetch uses it to check the RTC. This
bounds the error to the drift since the last fetch, whatever the temperature.
A response without `time`, or with a malformed one, still updates the forecast;
the sync is skipped with a warning.

### Comparing

1. The WiFi task notes `osKernelGetTickCount()` when the response headers
   arrive (`St67FetchResult::responseTick`).
2. The server's time at that moment is taken as `time` + 100 ms, the one-way
   delay after the server stamps it: measured from a PC, the whole HTTP round
   trip took 150–240 ms, and the stamp is taken late in handling the request.
   A whole-seconds `time` also gets 500 ms, the average lost to the truncation.
3. After the parse, about 1.3 s later once the ST67 has disconnected, the RTC is
   read, and the ticks since the headers are added to the server time.
4. The offset is RTC minus server. With milliseconds it is good to about
   ±150 ms, the spread of the network delay; with whole seconds to about
   ±0.6 s, mostly from the truncation.

### Deciding and stepping

The RTC is stepped when the offset is 250 ms or more either way, or 1 s with a
whole-seconds `time` (`Precision::adjustThresholdMs`). A smaller offset is
within the error of the comparison, and stepping it would add noise rather than
remove it. At the ~800 ppm the first board drifts, 250 ms builds up in about
5 minutes, so nearly every fetch steps the clock back by a fraction of a
second. After a
power-up, when the RTC has not been set, it is always set.

The RTC can only be set to a whole second, and starts that second when it is
written. So `stepToServer()` waits for the server's next second to begin, up to
a second, and writes it then; the log gives how many ms late the write was,
normally 0. It goes through the same path as `time set`, so the time is marked
set and survives a reset. The first sync on the first board stepped the RTC by
−28.574 s; `tools/rtc_offset.py` then found it within 10 ms of the PC clock.

The ST SNTP client in `LWIP/App/sntp.c` also writes the RTC when it runs. It is
not started, and must stay off, or the two would fight.

### Drift measurement

Each sync also measures the drift since the previous one:
`drift = offset − (what the previous sync left)`, where a step leaves 0 and a
kept RTC leaves its offset. `ClockSync::DriftTracker` sums these from the first
sync of the measurement, so the rate gets more precise as the span grows, and
the errors of the syncs in between cancel. The error is the sum of the errors
of the two ends over the span. With milliseconds that is 2 × 150 ms: ±83 ppm
after an hour, ±50 ppm after 100 minutes. With whole seconds it is 2 × 0.6 s:
±333 ppm after an hour, ±50 ppm after 6.7 h.

The measurement restarts at a reset, `time set`, a `time trim` that changes
the value, a sync that finds the RTC unset, and a jump too large to be drift:
more than the two syncs' errors plus 5 000 ppm of the interval, such as a DST
change.

### Log

Every sync logs the comparison and the decision, then the drift. From the first
board, with the whole-seconds server, a sync that stepped the RTC after a flash:

```
Clock sync: server 2026-09-23 09:37:54 (response 1312 ms ago), RTC 2026-09-23 09:38:24.486, offset +28.574 s (RTC minus server, +-600 ms)
Clock sync: RTC stepped by -28.574 s, as the offset reached the 1000 ms threshold; set to 2026-09-23 09:37:56 (+0 ms)
Clock drift: measurement starts at this sync, trim +18400 ppm
```

and a later one that kept it:

```
Clock sync: server 2026-09-23 09:45:21 (response 1213 ms ago), RTC 2026-09-23 09:45:22.843, offset +0.030 s (RTC minus server, +-600 ms)
Clock sync: RTC kept, the offset is under the 1000 ms threshold
Clock drift since the previous sync: -0.127 s over 111 s, -1145 ppm +-10820
Clock drift since 2026-09-23 09:43:31: -0.127 s over 111 s, -1145 ppm +-10820, -98.9 s/day; LSI 32551.5 +-346.2 Hz at trim +18400 ppm
Clock trim: too early to judge (+-10820 ppm); +-50 ppm needs 6.7 h of syncs with no reset, time set or time trim
```

and one with milliseconds:

```
Clock sync: server 2026-09-23 10:26:57.134 (response 1313 ms ago), RTC 2026-09-23 10:26:58.601, offset +0.054 s (RTC minus server, +-150 ms)
Clock sync: RTC kept, the offset is under the 250 ms threshold
Clock drift since the previous sync: +0.154 s over 4 min, +648 ppm +-3156
Clock drift since 2026-09-23 10:23:00: +0.154 s over 4 min, +648 ppm +-3156, +56.0 s/day; LSI 32609.9 +-101.0 Hz at trim +18400 ppm
Clock trim: too early to judge (+-3156 ppm); +-50 ppm needs 100 min of syncs with no reset, time set or time trim
```

That measurement started on a whole-seconds sync, so its ±3156 ppm is
(600 + 150) ms over 4 minutes.

The last line is the verdict on the trim. Once the error is within ±50 ppm it
says either that the trim is right to within the error, or which `time trim`
would cancel the drift. It never applies it: the LSI moves by about 800 ppm
with the conditions, so a trim taken from one span can be wrong for the next.

## Planned: Automatic Trim

The drift measurement above gives what an automatic trim needs, as
`trim' = (1 + trim) × (1 + ppm) − 1`. It already restarts on `time set`,
`time trim`, a reset and implausible jumps, and waits for ±50 ppm before
suggesting anything. Still to decide, so that one bad sample cannot spoil the
trim:

- Reject results far from the current trim, for example more than 1 000 ppm.
  Those point at a bad timestamp, not at the LSI.
- Move only part of the way, for example half, so noise averages out and a
  temperature swing does not cause overshoot.
- Save to the EEPROM only when the trim moves by more than a few ppm. The part
  is rated for 1 000 000 writes per page, and a save rewrites only the changed
  page.
- Log every adjustment with the drift, interval and new value, and show it in
  `status`.
