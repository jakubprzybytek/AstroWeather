# Astro Data Refresh

## Overview

An **astro refresh** is one complete update of the forecast shown on the
display: fetch the payload from the AstroWeather API over WiFi, check it, parse
it, set the clock from it, and publish it to the local board and the five
remote boards. `AstroDataRefreshTask` owns the whole transaction; no other task
fetches, parses or maps astro data.

A refresh starts from switch 1, the `astro refresh` console command, `wifi set`
and `wifi test`, or the built-in schedule, which runs at 00:10, 06:10, 12:10
and 18:10 local time and retries failures with a growing delay. While it runs,
the bottom matrix row of the local board shows a progress bar.

Only the HostController firmware has this feature.

The payload contract is maintained on the server side in
[`sst/docs/api-payload.md`](../../../sst/docs/api-payload.md). This document
describes what the firmware actually accepts, and where that differs from the
contract. See [Payload as parsed](#payload-as-parsed).

The design history, including the retired `SwitchTask` and the original
implementation phases, is in
[archive/Astro_Data_Refresh_Implementation_Plan.md](archive/Astro_Data_Refresh_Implementation_Plan.md).

## Software

| File | Responsibility |
| --- | --- |
| `User/Inc/HostController/AstroDataRefreshTask.hpp`, `User/Src/HostController/AstroDataRefreshTask.cpp` | The refresh task: triggers, pipeline, schedule driver, status summaries; draws the display mapping and the progress bar. |
| `User/Inc/HostController/AstroDisplayMapper.hpp`, `User/Src/HostController/AstroDisplayMapper.cpp` | Pure display mapping: `AstroData` to the six boards' numerics and matrix rows. Tested by `tests/AstroDisplayMapperTests.cpp`. |
| `User/Inc/HostController/AstroProgressBar.hpp`, `User/Src/HostController/AstroProgressBar.cpp` | Pure progress bar: row pattern per step, blink phase, and the outcome indicator's timing. Tested by `tests/AstroProgressBarTests.cpp`. |
| `User/Inc/Utils/Crc32.hpp` | The CRC-32 of the recheck. Tested by `tests/Crc32Tests.cpp`. |
| `User/Inc/HostController/AstroData.hpp` | The parsed model: six boards, the server `time` and `lastWeatherFetchTime`. |
| `User/Inc/HostController/AstroDataParser.hpp`, `User/Src/HostController/AstroDataParser.cpp` | Pure parser, no HAL or RTOS. Tested by `tests/AstroDataParserTests.cpp`. |
| `User/Inc/HostController/RefreshSchedule.hpp` | Pure schedule arithmetic: slots, retry delays, when a refresh is due. Tested by `tests/RefreshScheduleTests.cpp`. |
| `User/Src/HostController/MainLoopTask.cpp` | Switch 1 → `requestRefresh(RefreshTrigger::Switch1)`. |
| `User/Src/Console/AstroCommand.cpp` | `astro refresh`. |
| `User/Src/Console/SettingsCommand.cpp` | `wifi set` and `wifi test`, which request a `WifiTest` refresh. |
| `User/Src/Console/StatusCommand.cpp` | The `astro`, `weather` and `schedule` lines of `status`. |
| `User/Src/WiFi/St67HttpFetchTask.cpp` | The network fetch that the refresh waits on; see [WiFi.md](WiFi.md). |
| `User/Src/HostController/AppVariant.cpp` | Calls `init(&display)` after the WiFi fetch task has started, then starts the task. |

`AstroDataRefreshTask` is a `Task<3072>` at `osPriorityNormal`. The parsed
`AstroData` (about 700 B), the clock sync and the formatted log lines all sit on
its stack; 2048 bytes overflowed once the clock sync was added. The task object
also holds its own 4096-byte response buffer (`APP_ST67_HTTP_MAX_RESPONSE_BYTES`).

## Triggers

Every trigger goes through the same call,
`AstroDataRefreshTask::requestRefresh(RefreshTrigger)`, which returns at once:

| Trigger | Log name | Source |
| --- | --- | --- |
| `Switch1` | `switch1` | `MainLoopTask`, on a switch 1 press. |
| `Console` | `console` | `astro refresh`. |
| `Scheduled` | `scheduled` | The task itself, when the schedule says a refresh is due. |
| `WifiTest` | `wifi-test` | `wifi set` and `wifi test`: a refresh used to exercise the credentials, which ends with a verdict line. |

Only one refresh can be pending or running. A test-and-set of `active_` inside
a short critical section makes this race-free between `MainLoopTask`,
`ConsoleService` and the task itself. The result is one of:

| Result | When | Log |
| --- | --- | --- |
| `Accepted` | Idle; the run flag was posted. | `AstroDataRefresh trigger accepted source=<name>` |
| `Busy` | A refresh is pending or running. No second run is queued. | `AstroDataRefresh trigger ignored: active source=<name>` |
| `Unavailable` | The task has not started or has no display yet, or posting the thread flag failed. | `AstroDataRefresh trigger unavailable source=<name>` or `... trigger failed source=<name> status=<n>` |

`astro refresh` maps these to `OK astro-refresh=started`,
`ERR astro-refresh-busy` and `ERR astro-refresh-unavailable`. Any other line
starting with `astro` gets `ERR invalid-argument`. The command does not wait
for the refresh to finish; the outcome follows in the log. See
[Console.md](Console.md) for the exact replies of `wifi set` and `wifi test`.

## Refresh Pipeline

`executeRefresh()` runs these steps in order. Every path through it clears
`active_` and records an outcome.

1. **Fetch.** `FetchSt67Data()` hands the request to `St67HttpFetchTask` and
   waits. While it waits, the WiFi task's step (`FetchStage`) drives the
   progress bar. The result is logged:
   `AstroDataRefresh fetch status=<status> http=<code> bytes=<n> crc=<crc> detail=<n>`.
2. **CRC recheck.** The WiFi task computes a CRC-32 over the body as it
   receives it; the refresh task recomputes it over its own copy with
   `Crc32::compute()`, the standard reflected CRC-32
   (`AstroDataRefresh response crc-valid=0|1`). This checks the hand-over
   between the two tasks, not the network: the API has no checksum, and HTTP
   `Content-Length` is checked by the HTTP client.
3. **Raw payload log.** With a valid CRC, the body is logged in 64-character
   chunks, `AstroDataRefresh raw offset=<n> data="..."`, with every
   non-printable byte, including the line feeds, shown as `.`.
4. **Parse** into a local `AstroData`. The progress bar moves to its last
   segment first. A failure logs `AstroDataRefresh parse status=<status>` and
   leaves the display untouched.
5. **Last weather fetch.** The response's `lastWeatherFetchTime` is kept for
   `status`. A later failed refresh leaves it alone.
6. **Parsed data log**: the `lastWeatherFetchTime` line, then for each board
   its `nightId`, the four numerics and the four matrix rows as hex.
7. **Clock sync** from the `time` record; see
   [RTC.md](RTC.md#sync-from-the-api). A missing or malformed `time` skips the
   sync with a warning but keeps the forecast.
8. **Publish** to all six boards and `Display::submit()`; see
   [Display Mapping](#display-mapping).
9. **Schedule outcome.** The scheduler records success or failure; a success
   with the clock set is also written to RTC backup register `DR1`. The next
   slot or retry is logged.
10. **Summary.** `lastRefresh()` gets the outcome, trigger, fetch status, HTTP
    status and finish tick, and `active_` is cleared.
11. **Progress indicator**: full bar for success, blinking partial bar for
    failure; see [Progress Bar](#progress-bar).
12. `AstroDataRefresh complete source=<name> outcome=<outcome>`. This line is
    printed whatever the outcome, so it is not evidence of success on its own.
13. **WiFi test verdict**, for a `WifiTest` trigger only: one line saying
    whether the connection to the stored SSID worked and whether the forecast
    then came through, for example
    `WiFi test passed: connected to '<ssid>' (channel 6, -52 dBm) and fetched the forecast.`

The outcome is one of `ok`, `fetch-failed`, `crc-failed`, `parse-failed` and
`publish-failed`, or `never` before the first refresh. `publishDisplay()`
fails only when the task has no display, which `requestRefresh()` already
rules out, so `publish-failed` cannot happen in practice. A remote board that
does not answer is logged by the display code, not reported as a refresh
failure; see [Display.md](Display.md).

## Payload as Parsed

`parseAstroData()` in `AstroDataParser.cpp` reads the body one line at a time
into a 96-byte buffer, without allocating, and writes the result only once the
whole payload has validated.

### Lines and records

- Lines end in LF; a CR anywhere in a line is dropped, so CRLF works.
- Empty lines are skipped anywhere.
- A line may hold at most 95 characters. A longer line ends parsing there,
  which normally reports `truncated`.
- Each record is split at the first `=`. A non-empty line without `=` is
  `malformed`.
- The last line need not end in LF.

### Header

1. The first record must be `protocol`, and its value exactly `1`.
2. The second must be `configurationId`, with a value of at most 20
   characters. The value is not compared with the configuration requested.
3. Before `display=0`, `time` and `lastWeatherFetchTime` are recognised in
   either order; only the first of each counts. Any other key is ignored.

| Record | Accepted form | Bad value |
| --- | --- | --- |
| `time` | `YYYY-MM-DDTHH:MM:SS` or `YYYY-MM-DDTHH:MM:SS.mmm`, a real date in 2000..2099, then optionally `Z` or a UTC offset `+HH:MM` / `-HH:MM` | Kept as present but invalid; the clock sync is skipped, the forecast is kept. |
| `lastWeatherFetchTime` | `YYYY-MM-DDTHH:MM:SS`, no milliseconds, with the same optional offset, or `?` for no weather | Kept as present but invalid; reported by `status`, the forecast is kept. |

Both may be absent.

### Display blocks

There must be exactly six blocks, `display=0` through `display=5`, in order.
Within each block the records must come in this order:

| # | Key | Value |
| --- | --- | --- |
| 1 | `board` | Exactly `num4x4_matrix5x21`. |
| 2 | `nightId` | Not validated; kept (up to 15 characters) for the log. |
| 3 | `numeric_0` | Sunset, `HH:MM` or `?`. |
| 4 | `numeric_1` | Sunrise, `HH:MM` or `?`. |
| 5–8 | `matrix_0` … `matrix_3` | 21 characters from `*`, `.` and `?`, or the single `?`. |
| 9 | `numeric_2` | Maximum temperature, or `?`. |
| 10 | `numeric_3` | Minimum temperature, or `?`. |

- An unknown key anywhere in a block, or after the last block, is ignored.
- A known block key out of place is `missing-record`, as is a `display` record
  before the current block is complete, or after `display=5`.
- A time is two digits, a colon, two digits. The digits are not range-checked;
  the numeric display decides what it can show.
- A temperature is an optional `+` or `-`, one or more digits, a point and
  exactly one digit: `18.5`, `-2.0`. `18` and `18.50` are rejected.
- In a matrix row, `*` is on; `.` and `?` are off. A whole-row `?` is all off.
- `?` for a numeric is valid unavailable data, not a parse failure.

### Parse results

| Status | Log name | Cause |
| --- | --- | --- |
| `Success` | `success` | Six complete blocks. |
| `InvalidArgument` | `invalid-argument` | Empty body. |
| `Malformed` | `malformed` | A line with no `=`. |
| `UnsupportedProtocol` | `unsupported-protocol` | `protocol` is not `1`. |
| `UnsupportedBoard` | `unsupported-board` | `board` is not `num4x4_matrix5x21`. |
| `MissingRecord` | `missing-record` | `protocol` or `configurationId` not first, `configurationId` over 20 characters, or a block record missing or out of order. |
| `InvalidDisplay` | `invalid-display` | The first block is not `display=0`, or a block index is not the next one. |
| `InvalidTime` | `invalid-time` | `numeric_0` or `numeric_1` is neither `?` nor `HH:MM`. |
| `InvalidTemperature` | `invalid-temperature` | `numeric_2` or `numeric_3` is neither `?` nor a one-decimal number. |
| `InvalidMatrix` | `invalid-matrix` | A matrix row of the wrong length or with another character. |
| `Truncated` | `truncated` | The body ended, or a line was too long, before six complete blocks. |

### Differences from the contract

The firmware follows `api-payload.md` except in these details:

- The contract lists the header as `protocol`, `configurationId`, `time`,
  `lastWeatherFetchTime`, in that order. The firmware requires only the first
  two in order and accepts the other two in either order.
- The contract has one empty line between sections. The firmware ignores empty
  lines wherever they are.
- `configurationId` is limited to 20 characters by the firmware, not by the
  contract.
- The contract gives `time` always with milliseconds and both times always
  with a `+HH:MM` / `-HH:MM` UTC offset. The firmware also accepts whole
  seconds, no offset, and `Z`.
- The UTC offset is kept and shown by the log and `status`, but the RTC is set
  from the wall-clock part only; see [RTC.md](RTC.md#limits).
- A bad `time` or `lastWeatherFetchTime` does not reject the payload; the
  contract's rejection rules cover only required records.
- `nightId` is not checked to be a date, and times are not range-checked.
- The contract rejects a body shorter than its `Content-Length`. That check is
  in the HTTP client; the parser only sees whether six blocks arrived.
- An error payload (`error=...`, with HTTP 404 or 500) never reaches the
  parser: only a 2xx response with the expected content type counts as a
  successful fetch.

## Display Mapping

`AstroDisplayMapper::mapAll()` fills the boards and `publishDisplay()` then
submits them. Block *n* of the payload goes to one board:

| Block | Board | I2C address |
| --- | --- | --- |
| `display=0` | Local board | — |
| `display=1` … `display=5` | `display.remote(0)` … `display.remote(4)` | `0x10` … `0x14` |

On each board:

| Payload | Board element | How it is drawn |
| --- | --- | --- |
| `numeric_0` | Numeric 0 | `setTime(hour, minute)`: hour without a leading zero, colon lit. |
| `numeric_1` | Numeric 1 | `setTime(hour, minute)` |
| `numeric_2` | Numeric 2 | `setValue(value, 1)`: one decimal, for example `18.5`, `-2.0`. |
| `numeric_3` | Numeric 3 | `setValue(value, 1)` |
| `matrix_0` … `matrix_3` | Matrix rows 0–3 | `setRow()`, character *i* to column *i*. |
| — | Matrix row 4 | Cleared on the remote boards. Left alone on the local board, where it carries the progress bar. |

A numeric `?` is drawn as the decimal point on all four digits and nothing in
the indicator slot. That is distinct from the numeric display's own error
pattern, segment D (an underscore) on all four digits, which a value the
display cannot show gets instead: a temperature outside -99.9 … 999.9, or an
hour above 99. See [Display.md](Display.md#numeric-representation).

Temperatures between -1 and 1 keep the zero before the decimal point: -0.5
shows as `-0.5` and 0.5 as `0.5`. See
[Display.md](Display.md#fixed-point-values).

Only a fully parsed payload is published. After a fetch, CRC or parse failure
the display keeps whatever it showed.

### Local numerics 2 and 3

On the local board, numeric 2 and numeric 3 have other owners, and both are on
by default:

- **Numeric 2 shows the current sense reading** while `adc display` is on
  (default on). It is rewritten ten times a second, so block 0's maximum
  temperature is overwritten within 100 ms. See [CurrentSense.md](CurrentSense.md).
- **Numeric 3 shows the clock** while `time display` is on (default on). The
  clock redraws only when the minute changes, so block 0's minimum temperature
  shows for up to a minute after each refresh, then the time comes back. See
  [RTC.md](RTC.md#software).

So with the defaults, block 0's `numeric_2` and `numeric_3` are effectively
hidden. They show only with `adc display off` or `time display off`: turning
the owner off stops it writing, and the next refresh's value then stays.
Display setters are last-writer-wins by design; there is no field ownership.

## Progress Bar

While a refresh runs, the bottom row (row 4) of the local board shows its
progress. The row is split into six segments, one per step:

| Segment | Columns | Step (`FetchStage`) |
| --- | --- | --- |
| 1 | 0–2 | Queued, starting the WiFi module |
| 2 | 3–6 | Joining WiFi |
| 3 | 7–9 | Getting an IP address |
| 4 | 10–13 | Downloading |
| 5 | 14–16 | Disconnecting |
| 6 | 17–20 | CRC check, parse and publish |

- Finished steps are solid; the current one blinks every 250 ms.
- **Success**: the full row for 1.5 s, then clear.
- **Failure**: the bar up to and including the failed step blinks every 500 ms
  for 60 s, then clears. A fetch failure shows at the step the WiFi task
  stopped at; a CRC, parse or publish failure at segment 6.
- A new refresh clears the indicator at once.

The patterns and timing are `AstroProgressBar`'s; the refresh task keeps its
`Indicator` state and draws the bar itself, from the WiFi task's progress callback
and its own wake-ups, using `Display::submitLocal()`, so it never costs I2C
traffic and the WiFi task never touches the display. A scheduled refresh shows
the same bar as a manual one; there are no quiet hours. Typical step times and
an SWD check of the row are in [Display.md](Display.md#refresh-progress).

## Schedule

`RefreshSchedule.hpp` is pure arithmetic on local seconds since 2000
(`Calendar::secondsSince2000()`) and RTOS ticks; the task reads the clock and
asks it.

### Slots

The device refreshes at **00:10, 06:10, 12:10 and 18:10 local time**. The
server ingests the Clear Outside weather at 00:00, 06:00, 12:00 and 18:00
Europe/Warsaw, so each refresh picks up weather at most 10 minutes old. The
12:10 slot also picks up the noon rollover, after which block 0 is the next
observing night. The RTC holds local time with DST applied, so the slots and
the server follow DST together.

### When a refresh is due

Only the time of the **last successful refresh** is kept. The latest slot is
done when that time is at or after the slot's start. So:

- **Any trigger counts.** A successful switch 1, `astro refresh` or
  `wifi test` refresh inside a slot means the scheduler skips that slot.
- **Missed slots are caught up once.** After a day offline one refresh runs,
  not four.
- **Clock steps.** A backward step makes the last success look like the
  future, so nothing runs until the next slot. A forward step past a slot
  runs it.
- **Unset clock.** With no success yet, or the clock not set after a power-up,
  a refresh is due at once. That refresh also sets the clock.

The last success is stored in RTC backup register `DR1`
(`RTC_ASTRO_REFRESH_BKP_REGISTER` in `Core/Inc/main.h`), next to the time
itself. `init()` restores it only when the clock is set and the register is
not zero. Like the time, it **survives a reset or flashing but not a power
loss**; see [RTC.md](RTC.md#reset-and-power-loss).

### Failures

A failed refresh, whatever started it, is retried after **2, 5, 10 and 20
minutes, then every 30 minutes**. Retries count RTOS ticks, so they also work
while the clock is unset. When the next slot starts, the backoff restarts from
it.

A fetch that failed for want of WiFi credentials (`no-wifi-credentials`) is
not retried: the next attempt is the next slot, or `wifi set`, which refreshes
by itself.

### Driver

There is no separate scheduler task. `run()` wakes at least every 60 seconds,
more often while it blinks an outcome, checks the schedule, and when a refresh
is due calls `requestRefresh(RefreshTrigger::Scheduled)` on itself. A slot
therefore starts up to a minute late, and the first refresh after boot runs
about a minute after start-up. After each refresh one of these is logged:

```text
AstroDataRefresh next scheduled at 18:10
AstroDataRefresh retry 2 in 5 min
AstroDataRefresh no retry without WiFi credentials; next scheduled at 18:10
```

Nothing is logged about the schedule while the clock is unset.

## Console

| Command | Effect |
| --- | --- |
| `astro refresh` | Request a refresh now. |
| `wifi set <ssid> [password]` | Store credentials, then run a `WifiTest` refresh. |
| `wifi test` | Run a `WifiTest` refresh with the stored credentials. |
| `status` | Includes three lines on the refresh. |

The `status` lines are:

- `astro`: the last refresh's outcome, how long ago and what started it; for
  a failed fetch, the fetch status or HTTP code.
- `weather`: when the server last fetched the weather, from the last response
  that parsed, and how long ago.
- `schedule`: the next slot or retry and the last success, for example
  `schedule   every 6 h from 00:10; next 18:10; last ok 2026-09-23 12:11`.

The exact text of each is in [Console.md](Console.md).

## Tests

Native tests, run with the other suites; see [Development.md](Development.md).

- `tests/AstroDataParserTests.cpp`: a valid six-block payload, time and matrix
  unavailable values, one malformed temperature, an unknown header key, and
  the `time` and `lastWeatherFetchTime` records in all their accepted and
  malformed forms.
- `tests/RefreshScheduleTests.cpp`: slot boundaries, retry delays, the first
  refresh being due at once, a success covering its slot, a manual refresh
  counting, missed slots caught up once, backoff and its reset at the next
  slot, tick wrap, no retry without credentials, an unset clock, and clock
  steps both ways.
- `tests/AstroDisplayMapperTests.cpp`: each numeric kind and `?`, matrix rows,
  row 4 kept on the local board and cleared on the remotes, each block on its
  board, and a parsed payload's `*`, `.` and `?` through to the board bits.
- `tests/AstroProgressBarTests.cpp`: segment layout, the segment per stage, the
  250 ms blink phases, the failed segment per outcome, the success hold, the
  failure bar per failed step, its 500 ms toggles and 60 s end, and tick wrap.
- `tests/Crc32Tests.cpp`: the check value, known vectors, empty input and
  piecewise updates.

Not covered by tests:

- Most parser rejections: unsupported protocol or board, missing and
  out-of-order records, bad display indexes, bad times and matrices,
  over-long lines, truncation, and `configurationId` over 20 characters.
- The refresh task itself: triggers and the busy guard, the pipeline order,
  drawing the mapping and the bar on the display, and restoring `DR1`.
- `astro refresh` and the `status` lines.

These have been checked on hardware only.

## Future Work

### Shift stale data at the noon rollover

Not implemented. If the 12:10 refresh keeps failing, every board shows data one
night old: block 0 is the night that has just ended. The payload already holds
six consecutive nights, so the firmware could keep the last parsed `AstroData`
(about 700 B) and, at local noon, publish it shifted by one night:

- board 0 shows what board 1 showed, and so on up to board 4;
- board 5 shows the unavailable pattern (four decimal points, matrix off);
- repeat at each following noon until a refresh succeeds, so the display stays
  correct, with fewer nights, for up to five days offline.

Shift only while the clock is set. Compare the stored `nightId` of board 0 with
the current observing night rather than counting noons, so a reset or a clock
step cannot shift twice, and discard the stored data once nothing is left to
shift. Weather values carry no expiry of their own and would simply age with
the data, as they do today.

### Low-power wake

A future low-power mode would replace the 60-second wake with an RTC alarm set
to `RefreshSchedule::nextSlotStart()` or the next retry.
