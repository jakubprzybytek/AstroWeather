# Testing

The host's native suites and what they cover, the log fake, the bench tests
and the behaviour the tests pin. How to run and write native tests, the shared
stubs and assertions, and the shared code's suites are in the firmware-wide
[Native Tests](../../Docs/Testing.md).

## Coverage by Module

Line coverage measured with the `NativeTests-Coverage` preset on 2026-09-23:
95% over the 24 files the suites then compiled (983 lines). The suites for the
shared code (`NumericDisplay`, `DisplayCodec`, `DisplayI2cProtocol`,
`DisplayAddress`, `Crc32` and the rest) are in `../Common/tests`; see the shared
[Native Tests](../../Docs/Testing.md#common-suites). The others are in `tests/`;
`error_log_tests` was added later and is not in the figure.

| Module | Suite | Line coverage |
| --- | --- | --- |
| Test infrastructure (stubs, log fake) | `test_support_tests` | — |
| `NumericDisplay` (`DisplayTypes`) | `numeric_display_tests` | 97% |
| `DisplayCodec` | `display_codec_tests` | 100% |
| `DisplayI2cProtocol` | `display_i2c_protocol_tests` | 100% |
| `DisplayAddress` | `display_address_tests` | 100% |
| `CurrentSenseConversion` | `current_sense_conversion_tests` | 100% |
| `CalendarDate` | `calendar_date_tests` | 100% |
| `RtcTrim` | `rtc_trim_tests` | 100% |
| `ClockSync` | `clock_sync_tests` | 100% |
| `RefreshSchedule` | `refresh_schedule_tests` | 95% |
| `AstroDataParser` | `astro_data_parser_tests` | 96% |
| `AstroDisplayMapper` | `astro_display_mapper_tests` | 100% |
| `AstroProgressBar` | `astro_progress_bar_tests` | 98% |
| `Crc32` | `crc32_tests` | 100% |
| `SettingsCodec` | `settings_codec_tests` | 89% |
| `HttpResponseParser` | `http_response_parser_tests` | 100% |
| `St67HttpRules` | `st67_http_rules_tests` | 100% |
| `St67ConnectDiagnosis` | `st67_connect_diagnosis_tests` | 100% |
| `St67FetchStatusMap` | `st67_fetch_status_map_tests` | 100% |
| `ErrorLog` | `error_log_tests` | not measured |
| `SettingsStore`, `Eeprom24AA04` | — | Not covered |
| `BufferedDisplayBoard` | — | Not covered |
| Console commands, `ConsoleService` line assembly | — | Not covered |
| `AstroDataRefreshTask` run loop, `HttpClient` socket loop | — | Only through the extracted units |
| `LogService`, `PcbDisplayBoard`, `ClockTask` RTC access, Wi-Fi session | — | Bench only |

## Not Covered Yet

- `Settings::Store` and the `Eeprom24AA04` driver: page splitting, write-cycle
  polling, writing only changed pages, a read failure reported as `ReadFailed`.
- `BufferedDisplayBoard`: retries, online/offline logging.
- The console: `ConsoleService` line assembly (CR/LF, the 127-character limit,
  the 8-deep queue) and each command group's `OK`/`ERR` replies.
- The `AstroDataRefreshTask` run loop and the `HttpClient` socket loop, reached
  only through their extracted units.
- `WifiConnectResult` and `St67FetchStatus` still sit in headers that include
  `cmsis_os2.h`, so their suites need `STUBS`.

## Log Fake

`tests/fakes/FakeLogService.cpp` implements the `LogService` API and records
each `log()`, `logf()` and `sendLine()` call synchronously, without the uptime
prefix, the queue or USB. `FakeLog::lines()`, `FakeLog::contains()`,
`FakeLog::clear()` and `FakeLog::dump()` read it back. This is what lets
console commands and task code link in a test.

## Bench Tests

Not worth unit testing: the real Wi-Fi session, `LogService` over USB,
`PcbDisplayBoard` multiplexing timing and RTC register access. A scripted
smoke test on `tools/astro_console.py capture` (boot, `status`,
`astro refresh`, check the success lines) and the ADC known-voltage test from
the [hardware review](../../../KiCad/Docs/Hardware_Review.md) cover them.

**HTTPS certificate cases** need no test
server and no module reflash. Build with
`cmake --preset Debug -DAPP_ST67_TLS_BENCH_ANCHOR_ISRG=ON` so the anchor is
ISRG Root X1, flash, and in one console session (`tools/console_capture.ps1`)
run `astro refresh` against, in turn, the production host (wrong CA),
`sha256.badssl.com` or `rsa2048.badssl.com` (positive control, expect
`http=200`), `wrong.host.badssl.com` (hostname mismatch),
`untrusted-root.badssl.com`, `self-signed.badssl.com` and
`ecc256.badssl.com` (other root); set each with `api host <name>` and
`api path /`, finish with `api default`. A refused handshake logs
`ST67 https failed: connect ...`; a completed one logs the HTTP status. Then
reconfigure with `=OFF` and rebuild before flashing a production image; the
fetch log line's `ca=` names the anchor in use.

## Known Issues Pinned by Tests

Behaviour the tests record as it is today, to be decided separately. Each
test that pins one says "current behaviour".

**HTTP response** (`http_response_parser_tests`, `st67_http_rules_tests`):

- Header names are matched case-sensitively (`Content-Length`,
  `Content-Type`), although HTTP header names are case-insensitive.
- Header lines must end in CRLF; bare-LF headers never terminate.
- Chunked `Transfer-Encoding` is not decoded: chunk markers are passed on as
  body, and only the server closing the connection ends it.
- `Content-Length` accepts a leading `+`, rejects a trailing space, and takes
  the last of duplicate headers.
- Body bytes that arrive in the same read as the blank line count against the
  2048-byte header buffer.
- The `Content-Type` check is a prefix search anywhere in the headers: extra
  parameters pass, case and spacing must match exactly, and a header such as
  `X-Content-Type:` can match first.

**Fetch status** (`st67_fetch_status_map_tests`):

- Failures at module-info, callback-register, disconnect and other stages all
  report `HttpFailure`; w6x-init, wifi-init and net-init report
  `DriverFailure`.
- A `Content-Length` over 4096 reports `HttpFailure`, not `ResponseTooLarge`:
  the response is refused before the body callback that sets the flag.
- `CleanupFailure` cannot happen for a client fetch.

**Wi-Fi diagnosis** (`st67_connect_diagnosis_tests`): reason code 0 on a
failed connect classifies as `Failed`.

**Payload parser** (`astro_data_parser_tests`): a payload cut in the middle of
a line parses that partial line as complete, so the error is the record's own
(`InvalidMatrix`, `InvalidTemperature`) rather than `Truncated`; the
`Truncated` check for that case in `AstroDataParser.cpp` is unreachable.

**Progress bar** (`astro_progress_bar_tests`): a processing failure blinks the
full bar, the same pattern as success; the blink phase follows the tick and
jumps at the 32-bit wrap.

**Numeric display** (`numeric_display_tests`): every negative value at
precision 3 needs five positions and shows the error pattern. The console's
`display show` accepts up to four digits before the point and three after
(at most 32767 as an integer), so it also takes values such as `-1.234` or
`123.45` that need five positions, and the board shows the error pattern.
