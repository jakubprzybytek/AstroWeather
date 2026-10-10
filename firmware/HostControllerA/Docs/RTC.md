# RTC and Clock

## Overview

The HostController keeps the date and time in the STM32G0B1 RTC and shows the
time as `HH:MM` on numeric display 3 of the local board. The date is tracked
but not displayed. The RTC runs from HSE / 32: the same 24 MHz crystal that
runs the system clock, divided to 750 kHz. A crystal is accurate to a few tens
of ppm, so the clock needs no trim to be useful; `time trim` remains for a
measured crystal error.

Every successful astro fetch compares the RTC with the time the server sends
and steps it when it is 250 ms or more out; it also logs the drift measured
across fetches and what that says about the trim. See
[Sync from the API](#sync-from-the-api). `time set` sets it by hand. The time
is kept over a reset or flashing, less the time the crystal was stopped. After
a power loss display 3 shows `--:--` until the next fetch or `time set`. See
[Reset and power loss](#reset-and-power-loss).

Only the HostController firmware has the clock; the DisplayController project
has no RTC configured.

## Clock Source

The G0B1 RTC can be clocked from three sources:

| Source | Status on this board |
| --- | --- |
| HSE / 32, from the 24 MHz crystal `Y301` | **Used**: 750 kHz. |
| LSE, a 32.768 kHz crystal on PC14/PC15 | Not fitted. PC14/PC15 are unused. |
| LSI, the internal RC oscillator, nominally 32 kHz | Not used, and switched off. |

HSI16 cannot clock the RTC, and `time hsi`, which trims it
([TimelineSync.md](../../Docs/TimelineSync.md#the-hosts-clock)), does not
change the clock.

The crystal's error against the server has not been measured yet; the drift
line of each sync ([Drift measurement](#drift-measurement)) gives it once a
measurement has run long enough. The LSI the RTC ran from before was 1.85 %
fast and moved by about 1000 ppm with the conditions; those measurements are
in [archive/RTC_LSI_Clock_2026-10.md](archive/RTC_LSI_Clock_2026-10.md) and
[archive/RTC_Drift_Measurements.md](archive/RTC_Drift_Measurements.md).

## CubeMX Configuration

In `HostControllerA.ioc`:

- **Timers → RTC**: *Activate Clock Source* and *Activate Calendar* ticked.
  Alarms, wake-up, tamper and the outputs are off.
- **Clock Configuration**: the RTC clock mux is set to **HSE / 32**, 750 kHz
  from the 24 MHz HSE (`HAL_RTC_MspInit()` selects
  `RCC_RTCCLKSOURCE_HSE_DIV32`). The LSI is not enabled.
- **RTC → Parameter Settings**: 24-hour format, binary data format,
  asynchronous prescaler **124**, synchronous prescaler **5999**, so
  750 000 / 125 / 6000 = 1 Hz exactly.

The RTC's bus clock, 16 MHz, is 21 times its 750 kHz clock; the G0 needs at
least 7 times for reading the calendar through the shadow registers.

The generated `MX_RTC_Init()` sets the time to 00:00:00, 1 September of year
0, on every boot. The `USER CODE BEGIN Check_RTC_BKUP` section in `main.c` returns
before that when the time has been set; `RTC_TIME_SET_MARKER` in `main.h`
defines the marker. That is the only change to generated code; see
[Development.md](../../Docs/Development.md) for the CubeMX rules and the next
section.

Changing the RTC's clock source is possible only with a backup-domain reset,
which `HAL_RCCEx_PeriphCLKConfig()` does by itself when the source differs
from the one in `RCC_BDCR`. It clears the time and the backup registers, so
the first boot of an image with a new source starts at `--:--`, as after a
power loss.

## Software

| File | Responsibility |
| --- | --- |
| `User/Inc/Clock/ClockTask.hpp`, `User/Src/Clock/ClockTask.cpp` | Draws the time, sets the time and trim, and owns RTC access. |
| `User/Inc/Clock/RtcTrim.hpp` | Pure maths: trim in ppm → prescalers and calibration. Tested by `tests/RtcTrimTests.cpp`. |
| `User/Inc/Clock/CalendarDate.hpp` | Pure maths: leap years, month lengths, the weekday the RTC needs, and dates to seconds and back. Tested by `tests/CalendarDateTests.cpp`. |
| `User/Inc/Clock/ClockSync.hpp` | Pure maths for the API sync: the constants, and the drift tracker. Tested by `tests/ClockSyncTests.cpp`. |
| `User/Src/Astro/AstroDataParser.cpp` | Parses the payload's `time` record. |
| `User/Src/Astro/AstroDataRefreshTask.cpp` | Calls `ClockTask::syncToServer()` after each successful parse. |
| `User/Src/Console/TimeCommand.cpp` | The `time` console commands. |
| `User/Src/AstroWeather.cpp` | Applies the stored display setting and trim, then starts the task. |
| `tools/rtc_offset.py` | Measures the RTC against the PC clock over SWD. |

`ClockTask` redraws only when the minute changes. It sleeps until the next
minute boundary, which it works out from the RTC seconds. `time set`, `time display`
and a trim change wake it at once. The task's timer and the RTC both run from
the crystal, but the trim and the tick's rounding differ slightly, so it may
wake a little early. It then sees the same minute and sleeps for the
remainder. A new minute can appear up to about 2 s late.

Display 3 is shared. An astro refresh writes all four numeric displays, so its
value shows until the next minute, when the time comes back. With `time display
off` the task blanks display 3 once, at the switch, and then leaves it alone:
the wakes that follow, including the RTC write of every refresh's clock sync,
do not blank it again, so the refresh's minimum temperature stays. Booting
with the setting off blanks nothing; display 3 keeps the board's no-data
pattern like the other displays.

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

A reset switches the HSE off until `SystemClock_Config()` of the new boot
starts it again, and the RTC does not count while its clock is stopped. A
reset therefore loses a few milliseconds, and flashing loses the whole time
the chip is held in reset while it is programmed, a few seconds. Neither has
been measured. The first sync after boot, about a minute later, corrects it.

Two other boot steps would otherwise disturb a running clock, because
rewriting the prescalers drops the part of the current second already counted:

- `HAL_RTC_Init()` rewrites them unless the calendar counts as initialised
  (`RTC_ICSR.INITS`), which the hardware sets only for a non-zero year. The
  date that `setDateTime()` writes covers this, as the RTC's two-digit year
  holds 2000..2099 and is never 0.
- `setTrim()` at boot compares the new prescalers with `RTC_PRER`, which still
  holds the trim from before the reset, not with the generated 124/5999. It
  rewrites them only if they differ.

The crystal runs from VDD, so the clock cannot keep time while unpowered even
if a battery were on VBAT: the registers would survive but not count. Keeping
time through a power cut needs a 32.768 kHz crystal (LSE) as well as a
battery.

## Console Commands

HostController only. See also `help time`.

| Command | Effect |
| --- | --- |
| `time show` | `OK time=2026-10-09 22:26:10.284 set=yes trim=+0ppm prediv=124/5999 calm=0`: date and time to the millisecond, whether it has been set since power-up, and the trim with the registers it produced. |
| `time set <YYYY-MM-DD> <HH:MM[:SS]>` | Set the date and time, 24-hour; seconds optional, 00 if left out. Kept over a reset; lost on power loss. |
| `time trim <ppm>` | Apply and save the trim. `0` removes it. Valid range ±1000. |
| `time display on\|off` | Show the time on display 3, or blank it once and leave it to other writers. Saved. |

`settings show` and `status` include the stored trim and display setting.

## Trimming

The trim is the RTC clock's error from 750 kHz, in ppm. It is positive when
the crystal is fast, which makes an untrimmed clock gain time. The trim is
stored as the `ClockTrim` settings record (tag `0x02`, see
[Settings.md](Settings.md)) and applied at every boot. It is limited to
±1000 ppm (`RtcTrim::kMaxTrimPpm`), far beyond any crystal. A saved value
outside that range is a trim measured for the LSI the RTC ran from before
(the first board had +19 300 ppm): the start-up (`startWithSettings()` in
`AstroWeather.cpp`) logs `Clock trim +19300 ppm was for the LSI; using 0` and
uses 0. It changes the value in RAM only; `time trim 0`, or saving any other
setting, writes it.

The RTC makes its 1 Hz tick by dividing its clock by
`(PREDIV_A + 1) × (PREDIV_S + 1)`. It can also discard `CALM` clock cycles out
of every 2²⁰, which slows it by roughly 0.95 ppm per cycle.
`RtcTrim::compute()` uses both:

1. Work out the trimmed clock, `F = 750 000 Hz × (1 + ppm / 10⁶)`.
2. Fix `PREDIV_A` at 124, so that 0 ppm divides exactly by 125 × 6000. Each
   step of `PREDIV_S` is then about 167 ppm.
3. Choose `PREDIV_S + 1 = floor(F / 125)`. Rounding down keeps the divided
   clock at 1 Hz or faster, so the calibration only ever removes cycles and
   the `CALP` bit is never needed.
4. Remove the remainder with `CALM = round(2²⁰ × (F − P) / P)`, where
   `P = 125 × (PREDIV_S + 1)`. The calibrated clock is
   `F × 2²⁰ / (2²⁰ + CALM)`, which then equals `P`.

Across the whole ±1000 ppm range the tick is then within 1 ppm (0.09 s a day)
of 1 Hz, assuming the trim is correct, and `CALM` stays under 200 of its 511.
A crystal 20 ppm fast keeps 124/5999 with `CALM = 21`; one 20 ppm slow takes
`PREDIV_S` down a step to 5998 and makes up the rest with `CALM`.

The prescalers can only be written in initialisation mode, which keeps the
time but drops the part of the current second already counted, up to a second.
A test measured 0.87 s lost each time. So `setTrim()` rewrites the prescalers
only when they change, which a small adjustment usually does not.
`HAL_RTCEx_SetSmoothCalib()` sets `CALM` with no added pulses, without
initialisation mode.

A trim is worth setting only once the drift measurement has settled to a few
ppm and shows a crystal error of more than a few tens of ppm; resyncing from
the API bounds the error regardless.

## Measuring the Drift

The API syncs are the everyday reference: each `Clock sync` line gives the
RTC's error against the server to about ±150 ms, and the drift tracker turns
them into a rate (see [Drift measurement](#drift-measurement)). For a
measurement independent of the network, the PC clock is the reference;
resync Windows time first (`w32tm /resync`, as administrator), since an
unsynchronized PC has been seen 0.7 s behind the server.
`tools/rtc_offset.py` reads the RTC over SWD in HOTPLUG mode, which leaves the
firmware running. It stamps the PC time before and after each read and keeps the
fastest of four reads, so one reading is good to a few tens of ms.

1. Set a trim, or 0 to measure the crystal as it is: `time trim <ppm>`.
2. Set the time with `time set`, aiming at a whole second. Sending it over an
   already-open port, rather than starting a tool that then connects, keeps the
   delivery delay to a few tens of ms.
3. Record a baseline: `python tools/rtc_offset.py`, which prints for example
   `2026-09-22 22:51:31 offset +0.043 s (+-0.051)`. The baseline absorbs the
   `time set` delivery delay, about 50 ms, so it does not count as drift.
4. Hours later, run it with that baseline and the trim in use:

   ```bash
   python tools/rtc_offset.py --baseline "2026-09-22 22:51:31" 0.043 --trim 0
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

## Limits

- **The time is lost when power is lost**, until the next astro fetch or
  `time set`. See [Reset and power loss](#reset-and-power-loss).
- **No time zone.** The RTC holds local time. The server sends the
  configuration's local time with daylight saving applied, so a DST change
  reaches the clock at the next fetch after it. The payload's UTC offset is
  logged but not stored with the clock, so `status` computes the age of the
  last weather fetch in local time, one hour off across a DST change.
- **Synced only as often as something fetches.** The scheduled refresh runs
  every 6 hours (see [AstroRefresh.md](AstroRefresh.md#schedule)),
  and switch 1, `astro refresh` and `wifi test` fetch in between. Each of
  those may step the RTC.
- **The drift measurement is in RAM.** A reset or flashing restarts it, though
  the time itself survives.
- **The date is tracked but never shown.** It is kept so that an API sync and a
  future date display have it. The RTC rolls it over, including leap years. Its
  two-digit year limits it to 2000..2099.
- **The crystal's error is not yet known.** It is typically a few tens of
  ppm; the drift measurement gives it once it has run long enough
  ([Drift measurement](#drift-measurement)).

## Sync from the API

The astro API's payload carries a `time` header record, the configuration's
local time when the server rendered the response, as
`YYYY-MM-DDTHH:MM:SS.mmm+HH:MM` with daylight saving applied; see
`sst/docs/api-payload.md`. The offset only names the zone; the RTC is set from
the wall-clock part before it. The firmware also accepts `time` in whole
seconds, without `.mmm`, and takes the precision of each comparison from the
form it gets (`ClockSync::Precision`). Each successful fetch uses it to check the RTC. This
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
remove it. At 20 ppm, 250 ms builds up in about 3.5 hours, so an hourly
fetch rarely steps the clock and a 6-hourly one does so by a fraction of a
second. After a power-up, when the RTC has not been set, it is always set.

The RTC can only be set to a whole second, and starts that second when it is
written. So `stepToServer()` waits for the server's next second to begin, up to
a second, and writes it then; the log gives how many ms late the write was,
normally 0. It goes through the same path as `time set`, so the time is marked
set and survives a reset.

The ST67 module has its own SNTP client (`W6X_Net_SNTP_*`), which the firmware
does not enable; it would set the module's clock, not the RTC.

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

Every sync logs the comparison and the decision, then the drift. The first
sync after a power-up, or after the first boot with a new clock source, sets
the RTC and starts the measurement:

```
Clock sync: server 2026-10-09 22:25:36.531; the RTC was not set since power-up, now set to 2026-10-09 22:25:39 (+0 ms)
Clock drift: measurement starts at this sync, trim +0 ppm
```

One 45 minutes later that kept it, on the crystal (2026-10-09):

```
Clock sync: server 2026-10-09 23:10:53.363 (response 1781 ms ago), RTC 2026-10-09 23:10:55.024, offset -0.220 s (RTC minus server, +-150 ms)
Clock sync: RTC kept, the offset is under the 250 ms threshold
Clock drift since the previous sync: -0.220 s over 45 min, -81 ppm +-110
Clock drift since 2026-10-09 22:25:36: -0.220 s over 45 min, -81 ppm +-110, -7.0 s/day; RTC clock 749939.3 +-82.5 Hz at trim +0 ppm
Clock trim: too early to judge (+-110 ppm); +-50 ppm needs 100 min of syncs with no reset, time set or time trim
```

A sync that steps the RTC instead logs, for example,
`Clock sync: RTC stepped by -0.312 s, as the offset reached the 250 ms threshold; set to <time> (+0 ms)`.
With a whole-seconds server the error is ±600 ms and the threshold 1000 ms.

The last line is the verdict on the trim. Once the error is within ±50 ppm it
says either that the trim is right to within the error, or which `time trim`
would cancel the drift. It never applies it: the user picks the span the
trim is taken from.

## Open Items

- **The crystal's error.** A measurement over a day of syncs with no reset,
  `time set` or `time trim`; set a trim only if it is more than a few tens of
  ppm.
- **The time lost over a reset or a flash**, while the crystal is stopped:
  expected a few milliseconds and a few seconds; not measured.
- **Automatic trim.** The drift measurement gives what it needs,
  `trim' = (1 + trim) × (1 + ppm) − 1`, but the trim is set by hand. With a
  crystal it is rarely worth it; if added, it should reject results far from
  the current trim, move only part of the way, save to the EEPROM only when
  the trim moves by more than a few ppm, and log each adjustment.
