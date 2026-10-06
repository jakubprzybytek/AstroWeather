# Native Test Plan (2026-09-23)

> Archived 2026-10-06. Current state: [Testing.md](../Testing.md) and the shared
> [Native Tests](../../../Docs/Testing.md).

The plan for the native tests as it was drawn up on 2026-09-23 and carried
out through phase 2.

## Phases

The plan was drawn up on 2026-09-23 from the documentation review.

| Phase | State |
| --- | --- |
| 0 Infrastructure | Done 2026-09-23 |
| 1 Tests without production changes | Done 2026-09-23 |
| 2 Extract pure logic | Done 2026-09-23 |
| 3 Device fakes | Not started |
| 4 Console commands | Not started |

### Phase 0: Infrastructure (done)

Delivered as described in [Writing a Test](../../../Docs/Testing.md#writing-a-test): `Expect.hpp` and
`add_native_test()`, the HAL/RTOS stubs with a fake clock and GPIO model, the
recording `LogService` fake, the `NativeTests-Coverage` preset with `gcovr`,
and the GitHub Actions job. `test_support_tests` checks the stubs and the fake
themselves. All suites that existed before were moved onto `Expect.hpp`, so a
run reports every failing case instead of stopping at the first.

### Phase 1: Tests Without Production Changes (done)

| Suite | What it pins |
| --- | --- |
| `display_i2c_protocol_tests` | Serialize/deserialize round trip, 36-byte message, command `0x01`; null, wrong-size and wrong-command messages rejected without touching the destination; bits 21-23 masked |
| `display_address_tests` | All 27 strap combinations (high = 2, low = 1, floating = 0), pins left analog without pull, `0x10 + id` and 0 for id 27 or more |
| `display_codec_tests` | Golden vectors from the wiring tables in [Display.md](../../../Docs/Display.md): every segment of every digit, L1-L3, the 21 matrix columns, the matrix row order (bottom row first) |
| `astro_data_parser_tests` | 95-character line limit, CRLF, blocks out of order or missing, truncated payload, `configurationId` of 20 and 21 characters, `protocol=2`, `?` rows and numerics |
| `numeric_display_tests` | The fixed-point rules after the fix below |

The one production change: `NumericDisplay::setFixed()` dropped the zero before
the decimal point, so -0.5 showed as `-5`. The tests were written first
(15 checks failed), then the fix: the digit left of the decimal point is always
shown, so -0.5 shows as `-0.5` and 0.5 as `0.5`. A value that then needs more
than four positions shows the error pattern; that includes every negative
value at precision 3.

### Phase 2: Extract Pure Logic (done)

Behaviour-preserving refactors; each task now calls the extracted unit:

| Extracted from | Unit | Suite |
| --- | --- | --- |
| `HttpClient.cpp` | `HttpResponseParser`: header end, status line, `Content-Length`, the 2048-byte header and 4096-byte body limits | `http_response_parser_tests` |
| `St67HttpFetcher.cpp` | `St67HttpRules`: host/path validation, `Content-Type` check | `st67_http_rules_tests` |
| `St67NetworkSession.cpp` | `St67ConnectDiagnosis`: reason code → result, scan fallback, failure messages | `st67_connect_diagnosis_tests` |
| `St67HttpFetchTask.cpp` | `St67FetchStatusMap`: failure stage → `St67FetchStatus` | `st67_fetch_status_map_tests` |
| `AstroDataRefreshTask.cpp` | `AstroDisplayMapper`: 6 blocks → local and remote boards | `astro_display_mapper_tests` |
| `AstroDataRefreshTask.cpp` | `AstroProgressBar`: row-4 pattern and its blink/hold timing | `astro_progress_bar_tests` |
| `AstroDataRefreshTask.cpp`, `St67HttpFetcher.cpp` | `Crc32` (header-only, shared by both) | `crc32_tests` |
| `CurrentSenseTask.cpp` | `CurrentSenseConversion`: VDDA from VREFINT, temperature, display value | `current_sense_conversion_tests` |

One deliberate behaviour change: a current reading above 9999 mA now always
shows the error pattern; before, a reading above 32767 mA wrapped to a wrong
value. RAM is unchanged; the Debug image grew by about 1.5 KB of flash.

Still reached only through the extracted units: the `AstroDataRefreshTask` run
loop and the `HttpClient` socket loop.

### Phase 3: Device Fakes

- `IEeprom` (or a template parameter) and a `FakeEeprom` that counts page
  writes: `SettingsStore` writes only changed pages, falls back to defaults,
  and reports a read failure as `ReadFailed` rather than `Blank`.
- `FakeI2cBus`: `Eeprom24AA04` page splitting and write-cycle polling timeout;
  `BufferedDisplayBoard` online/offline logging with the fake clock.

### Phase 4: Console Commands

- Extract `ConsoleService` line assembly: CR/LF, the 127-character limit,
  empty lines, the 8-deep queue.
- Per command group, input line → expected `OK`/`ERR` lines, using
  [Console.md](../Console.md) as the specification.
- Fix the `help wifi` claim that `wifi set` echoes the password.
- Make `display set` validate with the display's own fit rule; it still uses
  the range from before the phase 1 fix, so some values it accepts show the
  error pattern.

### Follow-ups Found in Phases 1-2

- Move `WifiConnectResult` (`St67HttpFetchTask.hpp`) and `St67FetchStatus`
  (`St67FetchTypes.hpp`) into plain headers without `cmsis_os2.h`, so
  `st67_connect_diagnosis_tests`, `st67_fetch_status_map_tests` and
  `astro_progress_bar_tests` no longer need `STUBS`.
- Decide on the pinned behaviour listed under
  [Known Issues Pinned by Tests](../Testing.md#known-issues-pinned-by-tests), in particular
  case-insensitive HTTP header names and reporting an oversized
  `Content-Length` as `ResponseTooLarge`.
- Remove the unreachable `Truncated` check in `AstroDataParser.cpp`.

### Targets

- After phase 2 (met): every pure-logic unit has a suite, and the known bugs
  have tests; 95% line coverage of the code the suites compile.
- After phase 4: 80% line coverage of `User/Src`, excluding the bench-only
  files, enforced in CI.

