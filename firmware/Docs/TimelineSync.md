# Timeline Sync

How the display boards keep their refresh in step with the host's: the shared
timeline, the host's schedule of syncs, what a board does with each, how the
boards' HSI16 clocks are trimmed and how to measure all of it. The display
itself (content, levels, encoding, refresh) is in [Display.md](Display.md), the
sync message on the wire in [I2C.md](I2C.md#timeline-sync).

Every board's refresh frames form a timeline: 20 ms frames (`kFrameMicros`,
50 Hz), numbered from the host's boot (`Display/Timeline.hpp`). The blink
phase and the `LED_1` heartbeat both run off the frame number, so boards on
the same timeline blink and flash together. The host's own refresh is the
reference and is never corrected; a display board runs free from its own boot
until the first sync arrives.

| Part | Where | Does |
| --- | --- | --- |
| `TimelineServo` | `Common/Inc/Display/TimelineServo.hpp` | Sets each frame's length from a rate and a phase correction |
| `TimelineSync` | `Common/Inc/Display/TimelineSync.hpp` | Decides, from the host's position and the board's, the jump, phase, rate and HSITRIM steps; pure |
| `PcbDisplayBoard` | `Common/Src/Display/PcbDisplayBoard.cpp` | Runs the frames, keeps the record that stamps read, applies a decision |
| `HsiTrim` | `Common/Inc/Device/HsiTrim.hpp` | Reads and sets `HSITRIM`; limits the steps ([Utilities.md](Utilities.md#hsi-trim)) |
| `DisplaySyncTask`, `SyncSchedule` | `HostControllerA/User/.../Display/` | Broadcasts the host's position and polls the boards' status |
| `TimelineFollower`, `I2cTarget` | `DisplayController/User/` | Stamps the arrival of a sync, applies the decision, answers the status read |

## The Timeline on Every Board

- **Frames.** `RefreshSequencer` numbers the frames, the first one 1.
  `addFrames(delta)` renumbers them without touching the timing, and the
  blink phase follows the new numbers from the next frame on.
  `setFrameAdjust(micros)` lengthens or shortens every frame from then on: the
  adjustment is shared out over the five slots' first, longest, passes, and
  what does not divide evenly goes a microsecond each to the first slots
  (-23 us is -5, -5, -5, -4, -4).
- **Heartbeat.** `heartbeatLit(frame)` is true for frame 1 of every 100-frame
  cycle (`kCycleFrames`, 2 s), so `LED_1` is lit for one 20 ms frame every
  2 s, starting with the first blink-on phase. The refresh interrupt writes
  it at every frame start, so a flashing `LED_1` shows that the refresh
  interrupt is running, and synced boards flash together.
- **Frame start.** At the first pass of each frame the refresh interrupt
  (`PcbDisplayBoard::startFrame()`) takes the frame's correction from the
  board's `TimelineServo`, sets it on the sequencer, writes `LED_1` and
  publishes a record of the frame: its number, its start time and its length
  on this board's clock. The record is double-buffered, so a reader in a
  higher-priority interrupt never sees half of one.
- **Stamps.** `PcbDisplayBoard::stampNow()`, from any interrupt or task, gives
  that record plus, read at that instant, the time now and the servo's
  correction still pending (`TimelineStamp`). Times are `Utils::microsNow()`
  microseconds of the board's own clock
  ([Utilities.md](Utilities.md#microsecond-clock)). `positionMicros()` turns
  a stamp into nominal microseconds from the start of frame 0, scaling the
  time into the frame by that frame's actual length, so a corrected frame
  still spans 20 ms of timeline.

On the host the servo stays at zero: its frames are exactly `kFrameMicros` of
its own clock.

## Syncs from the Host

`DisplaySyncTask` broadcasts the host's timeline position to the I2C
general-call address: it stamps its own timeline with interrupts off and sends
the frame number and the microseconds into it ([I2C.md](I2C.md#timeline-sync)).
The task runs above the normal tasks and suspends the scheduler for the
transfer, about 1 ms, so a due sync is not held up and no task runs between
the stamp and the START.

Every display board takes the message at the same instant and stamps its own
timeline in the address-match interrupt, before the data bytes, which an
interrupt on the host can hold up. The host's position at that instant is its
stamp plus `kSyncTransferMicros`, 130 us: the HAL's setup, START and the
address byte at 100 kHz, and the interrupt's entry. How long the seven data
bytes then took (630 us plus whatever held the host up) is in `g_displayStats`
as `syncDataMicros`; the stamp does not depend on it.

The host's schedule (`HostControllerA/User/Inc/Display/SyncSchedule.hpp`):

| When | What |
| --- | --- |
| At boot, and when a board asks or `time sync now` is given | A burst of four syncs, at 0, 10, 40 and 100 s. The first two set each board's phase, the last two measure its rate, over 30 and then 60 s, and pick its HSITRIM. A request during a burst is ignored: the burst serves it. A request within 10 s after a sync was sent, as a poll that follows a regular sync makes, takes that sync as the burst's first |
| After a burst | One sync every 120 s, counted from the burst's last |
| 15 s after boot, then every 60 s | A one-byte status read of each remote chain address, `0x10`-`0x15` except the host's own. A board that answers "wants syncs" starts a burst, logged as `Display sync: burst, 0x11 wants syncs` |

With no display board on the bus the broadcast is not acknowledged; that is
not an error. `time sync` on the console shows the broadcasts sent and
answered, the next one and each board's answer
([Console.md](../HostControllerA/Docs/Console.md#time)); `time sync now`
starts a burst.

The interval is 2 minutes because the rate a board uses is the average over
its last interval, and a board's clock against the host's moves between
intervals: the error at the end of an interval grows with the square of its
length (see [Accuracy](#accuracy)).

### A Board That Starts Later

A board that is reset or powered up while the host runs sets itself to
"wants syncs". The host's next poll, at most 60 s later, starts a burst; a
regular sync may reach the board first and only set its phase. From that
burst: the first sync jumps the board onto the host's frame numbers, the
second finds the phase within about 50 ms, the third (30 s) measures the rate
and steps HSITRIM, and the fourth (60 s) sets the rate and locks. Measured on
`0x11` on 2026-10-07, reset while the host ran: locked 1 min 42 s and
2 min 18 s after the reset (twice, with the current rules).

## Following the Host

On a display board, `TimelineFollower` runs on the `DisplayApp` task. For each
sync it compares the host's position with its own at the stamp, asks
`Display::TimelineSync` for a decision and applies it with
`PcbDisplayBoard::applySync()`: renumber the frames (and the record the next
stamp reads), set the phase still to correct, counted from the stamp, and set
the rate.

`TimelineServo` sets each frame's length from two corrections, added:

- **Rate**: this board's clock against the host's, in ppm, clamped to
  ±20000. A board running fast (positive) needs more of its own microseconds
  per frame. It is applied a whole microsecond at a time, 1 us per 50 ppm of
  a 20 ms frame, with the remainder carried, so any rate comes out exact on
  average.
- **Phase**: how far the board is behind the host (positive: shorter frames)
  or ahead (negative: longer frames). It is taken out at 100 us a frame
  (0.5 %), or 1000 us a frame (5 %) while more than 5 ms out, so the pass
  lengths, and with them the brightness, change only slightly.

`TimelineSync` decides on each sync, with the error being the host's position
less the board's and the pending correction what the servo still had to make
at the stamp:

| Case | Decision |
| --- | --- |
| The first sync, or an error less the pending correction larger than 50 ms plus 2.5 % of the time since the previous sync (more than drift can explain: the host rebooted) | Jump: renumber the frames by the nearest whole number of frames, and slew out the rest, at most half a frame. The rate is unknown again |
| Any other | Correct the phase by the error |
| At least 25 s since the previous sync (a burst's 30 s, give or take the host's scheduling) | Measure the rate: the drift is the error less the pending correction, and the average rate over the interval is the servo's rate less the drift divided by the interval |
| That interval under 100 s (a burst's 30 or 60 s) | Nudge the rate: the average weighed by its interval, against the rate the board had weighed by the intervals that set it (at most 5 minutes' worth). Each stamp is a millisecond or two out, so a 30 s interval's average is 70 ppm rough, 8 ms two minutes later |
| That interval 100 s or more (the regular 2 minutes, give or take the host's scheduling) | Set the rate to the average, a few ppm rough. Nothing is carried on from the previous interval: the rate moves in steps as often as in lines, and a trend doubles a step (see [Accuracy](#accuracy)) |
| The new rate more than 2500 ppm out | Suggest HSITRIM steps, rounded at 3300 ppm a step (about 0.33 %); slow wants a higher trim. After a step the servo has at most 1650 ppm left, so the clock has to wander 850 ppm before the step is undone |

A sync that finds a large error corrects it and measures the rate over the
whole interval, as any other; it does not ask for a burst, whose short
intervals could only measure the rate worse.

`locked()` is true once a sync has measured the rate; a jump or an HSITRIM
step clears it, since the rate is then unknown or has just changed. The board
answers the host's status read with it: `0xA1` locked, `0xA0` wants syncs.

`TimelineFollower` moves HSITRIM by the suggested steps, at most 8 steps from
the trim the board booted with (`kHsiLimit`) and never across an HSICAL band
edge. The servo's rate then gains 3300 ppm per step, the board unlocks, and
the next measured interval sets the rate afresh; the servo covers the rest, up
to 2 %. The counters, the last error, drift and rate and the trim are in
`g_displayStats`
([DisplayController Architecture](../DisplayController/Docs/Architecture.md#diagnostics)).

## Clocks and HSITRIM

Both boards clock everything from HSI16, an RC oscillator within a percent or
two of 16 MHz before trimming and not steady to a few ppm. `HSITRIM` (0-127,
64 by default) moves it about 0.33 % a step, measured on a G070 on
2026-10-05: 0.338 % on the bench board, which is why the rate after a step is
set afresh by a measurement and not computed.

- **Display boards** move their own HSITRIM to follow the host's rate (above),
  so the servo only has to cover what a step leaves, and a board's frames stay
  near 20 ms. Where they end up depends on the chip: the bench boards booted
  2100-5100 ppm slow of the host and settled at trim 65 or 66.
- **The host** sets its trim from the console, `time hsi`, saved to the EEPROM
  and applied at boot before the refresh starts. Trimming it close to 16 MHz
  keeps its own frame rate, FreeRTOS tick and I2C timing near nominal; the
  display boards follow it whatever its clock.

### Trimming the Host's HSI

`time hsi <0-127>` sets the host's HSITRIM: higher runs faster, 64 is the
chip's default. It applies at once, is saved
([Settings.md](../HostControllerA/Docs/Settings.md#tag-registry)) and is
applied at boot. `time hsi` alone shows the trim, HSICAL and the saved trim
([Console.md](../HostControllerA/Docs/Console.md#time)).

To measure, from `firmware/HostControllerA`, with no other program holding
the console's COM port:

1. Run `python tools/hsi_measure.py` (`--duration`, default 600 s; `--port`,
   default auto-detected; needs pyserial). It reads the trim with `time hsi`,
   turns on `stats on`, fits a line to the PC's arrival times of the reports
   the host sends every 5000 ms of its own clock, and prints the host's clock
   against the PC's and the trim to set:

   ```text
   host clock <ppm> ppm against the PC, from <n> reports over <s> s (worst arrival <ms> ms off the line)
   trim <old> -> <new>: 'time hsi <new>', about <ppm> ppm after; measure again to confirm
   ```

   or `trim <n> is already the closest; no change`.
2. Set the suggested value with `time hsi <new>`.
3. Run the tool again to confirm.

The PC's clock is the reference; its error, tens of ppm at most, does not
change the step chosen. Measured on 2026-10-05 against the PC at trim 64: the
host +2660 ppm (trim 63 estimated at about -650 ppm), the display board at
`0x11` -2147 to -2500 ppm.

### How Steady the Clocks Are

Against the PC's clock, over 2-minute windows on 2026-10-07 the host's HSI16
read +3052 to +3251 ppm with nothing in its log at those times; in 5-minute
windows +2639 to +3252 ppm. The display board's clock against the host's
moved by the same amounts the other way, a few ppm to 300 ppm between one
interval's average and the next. The board's own warm-up (after a reset, or
when `display all test` lit every element) adds a ramp of 50-150 ppm per
interval for 20-30 minutes. The cause of the host's own movement is not
known.

## Measuring

`DisplayController/tools/stats_log.py` reads `g_displayStats` from a running
board over SWD, without resetting it, and prints a line for each sync the
board has applied. It takes the field list from `Stats.hpp` and the address
from the ELF. The ST-LINK has to be on the display board (on the host it would
read the host's RAM), and the tool needs `arm-none-eabi-nm` and
`STM32_Programmer_CLI` on `PATH` ([Development.md](Development.md#prerequisites)):

```bash
cd firmware/DisplayController
python tools/stats_log.py --duration 3600 > sync.txt
```

```text
pc_time  elapsed_s  syncs jumps locked  error_ms  drift_ms  rate_ppm trim hsiChg  frames  data_us max_us
15:36:46    1699.4     22     2      1      9.55      9.55      1622   66      1   89051     764   1840
```

| Column | Meaning |
| --- | --- |
| `syncs`, `jumps` | Syncs applied and, of those, jumps (the first, and a host reboot) |
| `locked` | What the board tells the host's poll |
| `error_ms` | Host minus board at this sync, before the correction: the board's offset at the end of the interval |
| `drift_ms` | The drift over the interval that measured the rate (the error less the pending correction), only updated when it did |
| `rate_ppm` | The servo's rate after this sync |
| `trim`, `hsiChg` | `HSITRIM` now and the steps taken since the board booted |
| `frames` | Frames shown, from the refresh |
| `data_us`, `max_us` | The seven data bytes of this sync and the longest, from the address match |

The line after a reset has `frames` and `syncs` back at 0. A read that fails
or is implausible (while the probe reconnects) prints `read failed`. The host
side of the same run is the console's `time sync` (broadcasts sent and
answered, each board's `locked` or `wants-sync`) and `tools/hsi_measure.py`
for the host against the PC.

## Accuracy

The simulation in `timeline_sync_tests` runs one board against the host frame
by frame on the real schedule, with exact stamps. The worst offset after
settling:

| Board clock | HSITRIM steps | Worst offset |
| --- | ---: | ---: |
| -4800 ppm, the bench boards of 2026-10-05, with a real step of 0.338 % | 1 | about 0.2 ms at the first 2-minute sync, from the rounding of the rate; then about 1 us |
| -10000 ppm (1 %) | 3 | about 1 us |
| -2500 ppm, drifting +400 ppm an hour | 0 | about 2 ms at the end of every 2-minute interval: the rate is the last interval's average, *k*·*I*²/2 stale by the end of the next, for a drift of *k* per unit time |
| +1500 ppm, drifting -400 ppm an hour | 0 | about 2 ms, likewise |

On the bench the stamps are not exact and the clocks move in steps. Board
`0x11`, 14 regular 2-minute syncs from 15:10 on 2026-10-07: +7.3, -9.5,
+15.6, -16.5, +6.9, +16.3, +3.6, -0.5, -22.4, +6.1, +1.0, +0.1, -11.3 and
+9.6 ms, nine of them within 10 ms; locked throughout, no burst, no HSITRIM
step. The larger ones coincide with the host's HSI16 moving 100-200 ppm within
an interval ([How Steady the Clocks Are](#how-steady-the-clocks-are)); the
interval's length, and not the estimate, sets how much that costs. With
5-minute syncs the same bench found 30-50 ms, and 90 ms when the host's clock
stepped. Against 20 ms frames and a 1 s blink phase, a few milliseconds are
not visible; 20 ms is one frame.

A trend carried on from the previous interval would halve the error of a
steady ramp and double that of a step; the rate moves in both ways about as
often, so there is none. The history of how this was found is in the
[archive](archive/Timeline_Sync_Bench_2026-10-07.md).

## Tests

`../Common/tests/TimelineSyncTests.cpp` (`timeline_sync_tests`): the servo's
rate and slew, the jump, rate and HSITRIM decisions, `HsiTrim::allowedSteps()`,
the heartbeat frame, `positionMicros()`, and the simulated board against the
host's schedule above. The host's schedule is `sync_schedule_tests` in
`HostControllerA/tests`. See [Testing.md](Testing.md).

## Open Items

- A run over a day's temperature, on more than one board.
- The cause of the host's HSI16 moving 100-200 ppm within minutes, which sets
  the floor of the error (its supply and load are the first things to try).
- The host's stamp is taken with interrupts off, but interrupts are enabled
  again before the START; its refresh interrupt (up to 850 us) running in
  between delays the START and the board's stamp with it, unseen by the
  board's `syncDataMicros`. Not measured.
