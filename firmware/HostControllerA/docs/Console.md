# USB Console and Log

## Overview

The board enumerates as a USB CDC virtual COM port. That one port carries two
things:

- **The log.** Every task writes diagnostics through `LogService`, which is the
  only code that transmits on the port.
- **The console.** `ConsoleService` reads lines typed on the host, runs them as
  commands, and replies through the same log.

Both are HostController features in practice. The DisplayController variant
starts `ConsoleService` but never starts `LogService`, so it parses commands
and then drops every reply; see [DisplayController](#displaycontroller).

For building and flashing, and for recovering a COM port that has vanished, see
[Development.md](Development.md).

## Connecting

### Which port

The board's own USB connector shows up as a **USB Serial Device**
(`VID_0483&PID_5740`) with a COM number, `COM4` on the development
workstation. The ST-LINK probe adds a second port, the *STMicroelectronics
STLink Virtual COM Port*, which carries no firmware output. Use the first one.

- The baud rate is ignored, since USB CDC is not a UART. Any value works;
  `115200` is conventional.
- End each line with `\n`. `\r` is ignored, so `\r\n` also works.
- Only one program can hold the port at a time. Close Serial Monitor, HTerm or
  `astro_console.py` before opening it elsewhere, and before flashing.
- If the port is listed but will not open, or has disappeared, see
  [COM port disappears or will not open](Development.md#com-port-disappears-or-will-not-open).

### Welcome message

Opening the port prints a welcome, so a connected device is visibly alive even
with statistics off:

```text
[0:00:05:12] [INFO] OK connected to AstroWeather HostController, built 2026-09-23 10:12:40
[0:00:05:12] [INFO] Settings loaded from EEPROM: ok
[0:00:05:12] [INFO] Type 'help' for commands. Periodic stats are off; 'stats on' to switch.
```

The build time comes from `cmake/BuildInfo.cmake`, which regenerates it on
every build, so it identifies the flashed image. The settings line gives
`Settings::Store::describe()` of the boot-time load (`ok`, `blank`,
`bad-crc`, ...; see [Settings.md](Settings.md#decode-results)). It is repeated
here because the startup log is written before USB has enumerated and never
reaches the host.

The welcome is triggered from two CDC class requests, hooked in the `USER CODE`
section of `USB_Device/App/usbd_cdc_if.c`:

| Request | Bridge call | Why |
| --- | --- | --- |
| `SET_CONTROL_LINE_STATE` | `ConsoleService_OnHostLineState(dtr)` | A rising edge of DTR. pyserial and `astro_console.py` raise DTR on open. |
| `SET_LINE_CODING` | `ConsoleService_OnHostLineCoding()` | Every terminal sets the line coding on open, including ones that never raise DTR, such as HTerm by default. |

Both only set a thread flag; the welcome is sent from the console task. A
program that sends both does so within milliseconds, so the task prints at most
one welcome per second (`kWelcomeHoldoffMs`). Changing the baud rate in a
terminal that is already open sends `SET_LINE_CODING` again and repeats the
welcome.

### VS Code Serial Monitor

For manual sessions, the `eclipse-cdt.serial-monitor` extension:

1. Run **Serial Monitor: Start Monitoring** from the Command Palette.
2. Select the USB Serial Device port and any baud rate, such as `115200`.
3. Set the line ending to LF or CRLF.
4. Type `help` and press Enter.

### astro_console.py

`tools/astro_console.py` is the scriptable client. It needs pyserial
(`python -m pip install pyserial`) and is run from the `HostControllerA`
directory.

```bash
python tools/astro_console.py list                          # ports and descriptions
python tools/astro_console.py send status                   # one command
python tools/astro_console.py send "adc log on" "adc log off"
python tools/astro_console.py shell                         # interactive, Ctrl+C exits
python tools/astro_console.py capture --duration 20 --command "astro refresh" --output capture.txt
```

| Option | Applies to | Default | Meaning |
| --- | --- | --- | --- |
| `--port COM4` | all, before the subcommand | auto | Port to open. Auto-detection skips any port whose description contains `ST-LINK` or `STLink` and takes the one left; it fails if none or several remain. |
| `--baud N` | all, before the subcommand | `115200` | Passed to pyserial; ignored by the device. |
| `--wait S` | `send` | `2` | Seconds to read after each command. |
| `--duration S` | `capture` | `15` | Total capture time. |
| `--command TEXT` | `capture` | none | One command to send during the capture. |
| `--send-at S` | `capture` | `3` | When to send `--command`, in seconds from the start. |
| `--output FILE` | `capture` | stdout | Write the capture to a file. |

Each command is sent with `\r\n`. Opening the port raises DTR, so the output of
every invocation starts with the welcome. `send` only reads for `--wait`
seconds, which is too short for the asynchronous result of `astro refresh` or
`wifi test`; use `capture` or `shell` for those.

When something is not covered, extend the tool rather than writing a throwaway
pyserial script.

## Output Format

Every line on the port, a command reply or a log message, is a log record:

```text
[d:hh:mm:ss] [LEVEL] text
```

The prefix is uptime from `HAL_GetTick()`, with days unpadded. Command replies
are logged at `INFO`, so a reply looks like
`[0:00:05:20] [INFO] OK time-display=on`. The examples below leave the prefix
out.

| Level | Tag | ANSI colour |
| --- | --- | --- |
| `Info` | `INFO` | none |
| `Warn` | `WARN` | yellow, `ESC[33m` |
| `Error` | `ERR` | red, `ESC[31m` |
| `Debug` | `DEBUG` | dark grey, `ESC[90m` |

A coloured line is sent as `ESC[..m` + text + `ESC[0m` + `\n`.

Log messages from other tasks arrive interleaved with command replies. Replies
to a single command are queued together by the console task and are not split
by other output in practice, but asynchronous results, such as those of
`astro refresh`, follow later among ordinary log lines.

### ST67 driver messages

The ST67W6X network driver logs through `vLoggingPrintf()`, which
`User/Src/WiFi/St67HttpFetchTask.cpp` redirects into `LogService`. The driver's
`LOG_LEVEL` is `LOG_DEBUG` (`ST67W6X_Network_Driver/Target/logging_config.h`),
so its info and debug messages both appear, at the `DEBUG` level. Errors map to
`ERR` and warnings to `WARN`. The driver's file and line are dropped and each
message is truncated to 95 characters. Driver messages usually end in their own
`\n`, so they are followed by a blank line. Raw AT command logging
(`W61_AT_LOG_ENABLE`) is off.

## Log Service

`LogService` (`User/Src/Debug/LogService.cpp`) is a `Task<1536>` at normal
priority, started first in the HostController's `AppVariant_Init()`.

Producers call `log()`, `logf()` or `sendLine()` from task context. The call
formats the prefix and message into a 200-byte record, puts it on a static
16-entry CMSIS queue without blocking, and wakes the task. If the queue is full,
the oldest record is discarded to make room; each lost record counts as
`dropped`. The calls are not ISR-safe.

The task drains the queue and hands each record to `CDC_Transmit_FS()`, the only
caller of that function in the application. While the transport reports
`USBD_BUSY` it retries every 5 ms, for up to 40 ms, then drops the record and
counts `busyDrop`. Any other failure also counts `busyDrop`. `CDC_Transmit_FS()`
reports busy before USB has enumerated, and the previous packet stays pending
while no program has the port open, so output written with the port closed is
lost rather than buffered. A queued record is removed before it is sent, so an
accepted record can still be lost here.

### Burst size

The console and log tasks have the same priority, so a command's whole reply is
normally queued before any of it is sent. A reply of more than 16 lines would
push its own first lines out. `help` groups and `status` are kept under 16
lines by `static_assert`s and comments in `HelpCommand.cpp` and
`StatusCommand.cpp`. `eeprom dump` prints 32 lines and relies on the log task
getting time between the I2C reads; a `dropped` count that rises during a dump
means lines were lost.

### Statistics

`stats on` switches on periodic telemetry: once immediately, then every 5
seconds on a fixed schedule, whatever else is being logged. It is off at every
boot and not saved. All of it is sent at the `Debug` colour and bypasses the
queue.

```text
[0:00:06:40] [STATS] sent=412 dropped=0 busyDrop=37
[0:00:06:40] [MEM] heapFree=24752 heapMin=19352
[0:00:06:40] [STACK] name=LogService configured=1536 remaining=652
[0:00:06:40] [STACK] name=ConsoleService configured=2048 remaining=1180
...
```

| Field | Meaning |
| --- | --- |
| `sent` | Queued records transmitted, command replies included. Statistics lines are not counted. |
| `dropped` | Records lost because the queue was full. |
| `busyDrop` | Lines lost because USB stayed busy or no host had the port open. Statistics lines are counted here. |
| `heapFree`, `heapMin` | `xPortGetFreeHeapSize()` and `xPortGetMinimumEverFreeHeapSize()`, in bytes. |
| `configured` | The task's stack size in bytes. |
| `remaining` | Stack high-water mark in bytes: the part of the stack never yet used. |

There is one `[STACK]` line for each task created through `Task<>`. The counters
are cumulative since boot. See [Firmware-RAM-Usage.md](Firmware-RAM-Usage.md)
for what the stack figures mean for sizing.

## Console Service

`ConsoleService` (`User/Src/Console/ConsoleService.cpp`) is a `Task<2048>` at
normal priority.

### Receive path

1. `CDC_Receive_FS()` in `usbd_cdc_if.c` runs in the USB interrupt and passes
   each packet to `ConsoleService_OnUsbRxData()`, declared in
   `User/Inc/Console/ConsoleServiceBridge.h`, then rearms reception.
2. The bytes go into a 256-byte single-producer, single-consumer ring, and the
   task is woken. Bytes that arrive while the ring is full are discarded
   silently.
3. The task assembles lines from the ring:
   - `\r` is ignored and `\n` ends the line.
   - A line holds at most 127 characters. Anything longer is discarded up to
     the `\n` and answered with `ERR line-too-long`.
   - An empty line is ignored.
4. A complete line goes onto an 8-deep command queue. If it is full, the line is
   dropped with `ERR command-queue-full`.
5. The task runs queued commands one at a time, on its own stack.

Input is not echoed. The only output for a command is its reply.

### Dispatch

Each handler is tried in turn; the first that recognises the line owns it:

1. `help` (`HelpCommand.cpp`)
2. `status` (`StatusCommand.cpp`)
3. `stats on`, `stats off` (in `ConsoleService.cpp`)
4. `astro ...` (`AstroCommand.cpp`), HostController only
5. `time ...` (`TimeCommand.cpp`), HostController only
6. `adc ...` (`AdcCommand.cpp`)
7. `settings ...` and `wifi ...` (`SettingsCommand.cpp`)
8. `eeprom ...` (`EepromCommand.cpp`)
9. `display ...` (`DisplayCommand.cpp`)

A line no handler recognises gets `ERR invalid-command`. Commands are
case-sensitive. Fixed commands such as `stats on` or `settings show` must match
exactly, with single spaces; commands with numeric arguments are parsed with
`sscanf` and are more forgiving about spacing.

Handlers return a `Console::CommandResult`, and `ConsoleService` turns the
failures into these replies:

| Reply | Meaning |
| --- | --- |
| `ERR invalid-command` | No handler recognised the line. |
| `ERR invalid-argument` | The command was recognised but its arguments were not. |
| `ERR rtc-unavailable` | The RTC could not be read or written. |
| `ERR settings-unavailable` | No settings store, or the EEPROM write failed. A failed write is also logged as `[ERR] Settings save failed status=<n>`. |
| `ERR eeprom-unavailable` | No EEPROM in this variant, or the chip did not answer. |
| `ERR display-unavailable` | No display in this variant. |
| `ERR astro-refresh-busy`, `ERR astro-refresh-unavailable` | See [astro](#astro). |

Some handlers, `help`, `settings` and `wifi` among them, send their own more
specific `ERR` lines instead.

## Command Reference

Commands marked **HC** exist only in the HostController build. `help` and
`help <group>` list the same commands on the device.

### help

| Command | Reply |
| --- | --- |
| `help` | `OK help`, then an index of the commands and the group names. |
| `help <group>` | `OK help <group>`, then details and examples for that group. |
| `help <unknown>` | `ERR unknown help group '<name>'; Groups: stats, display, astro, time, adc, settings, wifi, eeprom` |

The groups are `stats`, `display`, `astro` (HC), `time` (HC), `adc`,
`settings`, `wifi` and `eeprom`. Help is split into groups to keep each reply
under the 16-line log queue.

### status

A one-screen summary, 13 lines on the HostController:

```text
OK status
firmware   HostController, built 2026-09-23 10:12:40
uptime     0d 00:03:11
heap       24752 B free, 19352 B lowest since boot
stats      off
eeprom     answering at 0x50, 512 bytes
settings   loaded at boot: ok; adc log off, adc display on, time display on, trim +18400 ppm, low brightness off
wifi       'MyNetwork' stored; last connect ok 0d 00:03:05 ago (channel 2, -39 dBm)
astro      last refresh ok, 0d 00:02:25 ago, from console
weather    last fetched by the server 2026-09-23 09:05:12 +02:00, 1 h 07 min ago
schedule   every 6 h from 00:10; next 12:10; last ok 2026-09-23 10:10
brightness normal
remote     0x10 no 0x11 no 0x12 no 0x13 no 0x14 no
```

| Line | Content and alternative forms |
| --- | --- |
| `firmware` | Variant and build time. |
| `uptime` | `d hh:mm:ss` from the RTOS tick, which wraps after about 49 days. |
| `heap` | Free FreeRTOS heap now and the lowest it has been. |
| `stats` | `on, every 5 s` or `off`. |
| `eeprom` | Probed now: `answering at 0x50, 512 bytes` or `NOT ANSWERING at 0x50`. `not present in this variant` when there is no EEPROM; the `settings` and `wifi` lines are then left out. |
| `settings` | The boot-time decode result, then the values in use. |
| `wifi` | `not configured; set credentials with 'wifi set <ssid> <password>'`, or `'<ssid>' stored;` followed by `not connected since boot ('wifi test' to try)`, `last connect ok <age> ago (channel <n>, <rssi> dBm)` or `last connect FAILED <age> ago: <reason>`. The reasons are listed in [WiFi.md](WiFi.md). |
| `astro` **HC** | `no refresh since boot; try 'astro refresh'`, `first refresh running now`, or `last refresh <outcome>, <age> ago, from <trigger>`. Outcomes are `ok`, `fetch-failed`, `crc-failed`, `parse-failed` and `publish-failed`; a fetch failure adds its cause in brackets, such as `(no HTTP response)` or `(http 404)`. Triggers are `switch1`, `console`, `scheduled` and `wifi-test`. `; another running now` is appended while a refresh is in progress. |
| `weather` **HC** | When the server last fetched the weather, from the last good response, with its age if the clock is set. Otherwise `last fetch time unknown until a refresh succeeds`, `... not reported by the server`, `... malformed in the response`, or `none on the server at the last refresh`. |
| `schedule` **HC** | The next slot and the last success. `next` reads `once the clock is set` before the time is known; after failures it reads `retry <n> in <s> s`, `retry <n> now` or `no WiFi credentials, then <HH:MM>`. `last ok` reads `none since power-up` until a refresh succeeds. See [AstroRefresh.md](AstroRefresh.md). |
| `brightness` **HC** | `normal` or `low`: the state in use. It normally matches `low brightness` in the `settings` line, since both controls save; see [display low](#display). |
| `remote` | Each remote display board, probed now on I2C: `yes` if it answered. `no display boards in this variant` without a display. |

WiFi has no link state of its own, so the `astro` line is the evidence that the
network path works.

### stats

| Command | Reply |
| --- | --- |
| `stats on` | `OK stats=on`, then an immediate report and one every 5 s. |
| `stats off` | `OK stats=off` |

See [Statistics](#statistics) for the report format.

### astro

**HC.** Details in [AstroRefresh.md](AstroRefresh.md).

| Command | Reply |
| --- | --- |
| `astro refresh` | `OK astro-refresh=started`. The fetch runs in the background; progress and the result follow in the log. |

`ERR astro-refresh-busy` means a refresh is already running, and
`ERR astro-refresh-unavailable` that the refresh task is not ready. Any other
line starting with `astro` gets `ERR invalid-argument`. The refresh task logs
`AstroDataRefresh trigger accepted source=console` (or
`... ignored: active source=console` as a warning) just before the reply.

### time

**HC.** Details in [RTC.md](RTC.md#console-commands).

| Command | Reply |
| --- | --- |
| `time show` | `OK time=2026-09-22 20:15:03.123 set=yes trim=+18400ppm prediv=3/8146 calm=26` |
| `time set <YYYY-MM-DD> <HH:MM[:SS]>` | `OK time=2026-09-23 10:15:00`. Seconds default to `00`. |
| `time trim <ppm>` | `OK time-trim=+18400ppm`. Range ±100000. Saved. |
| `time display on\|off` | `OK time-display=on` or `OK time-display=off`. Saved. |

`set=no` in `time show` means the clock has not been set since a power loss.
The `prediv` and `calm` values depend on the trim; see
[RTC.md](RTC.md#trimming). An invalid date or time, a trim out of range, or any
other `time` line gets `ERR invalid-argument`.

### adc

Details in [CurrentSense.md](CurrentSense.md).

| Command | Reply |
| --- | --- |
| `adc log on\|off` | `OK adc-log=on` or `OK adc-log=off`. Logs a current-sense reading ten times a second. |
| `adc display on\|off` | `OK adc-display=on` or `OK adc-display=off`. Shows the current on numeric display 2. |

Both are saved to the EEPROM at once. Any other `adc` line gets
`ERR invalid-command`.

### settings

Details in [Settings.md](Settings.md).

| Command | Reply |
| --- | --- |
| `settings show` | Three lines, shown below. |
| `settings save` | `OK settings-save` |
| `settings defaults` | `OK settings-defaults` |

```text
OK settings adc-log=off adc-display=on time-display=on time-trim=+18400ppm display-low=off
OK settings wifi-ssid=MyNetwork wifi-password=<set>
OK settings boot-load=ok
```

`settings show` prints the values the firmware is using. The password appears
only as `<set>` or `<unset>`, and an empty SSID as `<unset>`. `boot-load` is
what the EEPROM held at power-up, not its present content.

`settings save` rewrites the stored copy, which is needed only after
`boot-load` reported an error or after `eeprom erase`; the other commands save
as they change. `settings defaults` resets every value and saves: adc log off,
adc display on, time display on, trim 0, normal brightness, no WiFi. The running tasks keep their
current behaviour until the next boot.

Any other line starting with `settings` gets
`ERR unknown command '<line>'; see 'help settings'.`

### wifi

Details in [WiFi.md](WiFi.md).

| Command | Reply |
| --- | --- |
| `wifi set <ssid> [password]` | `OK wifi-set ssid='<ssid>' password=<set>; saved.`, or `... no password (open network); saved.` Then, on the HostController, a connection test starts. |
| `wifi test` | `OK wifi-test`, then a connection test. |
| `wifi clear` | `OK settings-wifi-clear` |

`wifi set` rules:

- The SSID is 1–32 characters. The password is 8–63 characters, or left out
  for an open network.
- An argument is a run of non-space characters, or a double-quoted string that
  may contain spaces: `wifi set "My Network" "my pass phrase"`. There is no
  escape for a `"` inside a value.
- Invalid input is rejected with a specific `ERR wifi-set: ...` line before
  anything is saved: a missing SSID, an unterminated quote, a value too long or
  too short, or more than two arguments.
- The whole line must fit the 127-character limit, which a quoted 32-character
  SSID and a quoted 63-character password do.

The test runs an ordinary astro refresh tagged `wifi-test`, so it also
publishes the forecast to the displays. It first replies
`Testing the connection to '<ssid>' now; the result follows in a few seconds.`,
or explains that a refresh is already running or the WiFi task is not ready.
The verdict follows as a log line such as
`WiFi test passed: connected to '<ssid>' (channel 2, -39 dBm) and fetched the forecast.`
or `WiFi test FAILED for '<ssid>': wrong password.`

`wifi test` with nothing stored replies
`ERR wifi-test: no credentials stored. Set them with 'wifi set <ssid> <password>'.`
Any other line starting with `wifi` gets
`ERR unknown command '<line>'; see 'help wifi'.`

### eeprom

Raw access to the 512-byte 24AA04 settings EEPROM at `0x50`, for bring-up and
debugging. Offsets and lengths are hex, matching the addresses `eeprom dump`
prints. `000`–`07F` holds the settings image; see
[Settings.md](Settings.md#image-layout).

| Command | Reply |
| --- | --- |
| `eeprom probe` | `OK eeprom-probe addr=0x50 size=512 page=16` |
| `eeprom scan` | `OK eeprom-scan found=0x50` for each device on I2C1, then `OK eeprom-scan devices=<n>`. The board's own slave address, `0x08`, is skipped. |
| `eeprom dump` | 32 lines of 16 bytes, `000: FF FF ...` to `1F0: ...`, with no `OK` line. |
| `eeprom read <offset> [length]` | The same hex lines for that range. `length` defaults to 1. |
| `eeprom write <offset> <hex>` | `OK eeprom-write offset=0x100 bytes=3`. Up to 32 bytes as an even number of hex digits without spaces, e.g. `eeprom write 100 A55A01`. |
| `eeprom erase` | `OK eeprom-erase`. Fills all 512 bytes with `FF`. |

A range past the end of the chip, malformed hex, or an unknown `eeprom` line
gets `ERR invalid-argument`. A chip that does not answer gets
`ERR eeprom-unavailable`. `eeprom erase` wipes the settings: the next boot uses
defaults unless `settings save` is run first. `eeprom write` into `000`–`07F`
changes the stored settings, and a bad CRC makes the next boot fall back to
defaults.

The scan finds the EEPROM on all of `0x50`–`0x57` and every remote
display board that is present; see [Settings.md](Settings.md#storage-medium).

### display

`set`, `time`, `blank` and `matrix` drive the local board only. Remote boards
are updated by `astro refresh`. Details in [Display.md](Display.md).

| Command | Effect |
| --- | --- |
| `display set <n> <value> <precision>` | Fixed-point number on numeric display `n` (0–3). `value` is −999 to 9999 and `precision` (0–3) the digits after the point. `display set 0 1234 2` shows `12.34`. |
| `display time <n> <HH:MM>` | A time on display `n`. `HH` and `MM` accept 00–99 and are not checked as a clock. |
| `display blank <n>` | Switches display `n` off. |
| `display matrix <row> <bits>` | One row (0–4, 0 at the top) of the 5×21 matrix. `bits` is a string of `0` and `1`, character N lighting column N; missing columns are off and extra ones ignored. |
| `display low on\|off` **HC** | Low brightness on every board: drives `LOW_POWER_ENABLE`. Saved, and applied at boot. Replies `OK display-low=on` or `OK display-low=off`. |
| `display low` **HC** | `OK display-low=<in use> saved=<saved>`, e.g. `OK display-low=on saved=on`. Switch 2 also saves, so the two differ only if a save failed, which logs `Settings save failed`. |

The other commands reply `OK display`. Out-of-range values or an unknown `display`
subcommand get `ERR invalid-argument`. Display 3 is also driven by the clock,
and the matrix by refresh progress, so a manual setting there may soon be
overwritten.

## Security

The WiFi password is stored in the EEPROM in the clear.

- `wifi set` is **not** echoed. The console does not echo input, and the reply
  and connection test log only the SSID. The driver's AT command log, which
  would contain the password, is compiled out. `help wifi` on current firmware
  still says the line is echoed; that text is out of date.
- `settings show` and `status` never print the password.
- `eeprom dump` and `eeprom read` over the settings area print it as hex bytes.
- The typed line does pass through the host's terminal, its scroll-back and any
  capture file.

Anyone with the USB port can also read and change the credentials, since the
console has no authentication.

## DisplayController

The DisplayController's `AppVariant_Init()` starts `ConsoleService` with no
display, EEPROM or settings store, and does not start `LogService`. Commands are
still received and dispatched, but `LogService` has no queue, so every reply and
the welcome are discarded. Commands that need a missing device would answer
`ERR display-unavailable`, `ERR eeprom-unavailable` or
`ERR settings-unavailable` (the last for all `settings` and `wifi` commands),
and the `astro` and `time` commands are not built.

## Fixed Limits

| Resource | Limit | Where |
| --- | ---: | --- |
| Console task stack | 2048 bytes | `ConsoleService.hpp` |
| RX ring | 256 bytes | `kRxRingSize` |
| Command line | 127 characters | `kMaxLineLength` = 128 |
| Command queue | 8 lines | `kCommandQueueDepth` |
| Welcome hold-off | 1 s | `kWelcomeHoldoffMs` |
| Console reply | 127 characters | `ConsoleService::reply()` buffer |
| Log task stack | 1536 bytes | `LogService.hpp` |
| Log queue | 16 records | `kLogQueueDepth` |
| Log record | 199 characters, prefix included | `kMaxLogMessageLen` = 200 |
| ST67 driver message | 95 characters | `vLoggingPrintf()` buffer |
| CDC busy retry | every 5 ms, for up to 40 ms | `kTxRetryDelayMs`, `kTxRetryWindowMs` |
| Statistics period | 5 s, while `stats on` | `kStatsPeriodMs` |

Longer text is truncated by `snprintf`.

## Code Layout

| File | Content |
| --- | --- |
| `User/Src/Debug/LogService.cpp` | Log queue, CDC transmit, statistics. |
| `User/Src/Console/ConsoleService.cpp` | RX ring, line assembly, command queue, dispatch, welcome. |
| `User/Inc/Console/ConsoleServiceBridge.h` | C entry points called from `usbd_cdc_if.c`. |
| `User/Src/Console/*Command.cpp` | One handler per command group. |
| `User/Src/Debug/FirmwareInfo.cpp` | Variant name and build time for the welcome and `status`. |
| `USB_Device/App/usbd_cdc_if.c` | CubeMX CDC glue; changes only inside `USER CODE` sections. |
| `tools/astro_console.py` | Host client. |

## Maintenance Notes

- Keep `CDC_Transmit_FS()` in `LogService` alone. New output goes through the
  log queue, not straight to the transport from another task or an interrupt.
- Keep the C bridge. CubeMX-generated C code must not include C++ headers.
- The log API is for task context. Logging from an interrupt would need an
  ISR-safe producer; only the USB RX path runs in interrupt context today.
- A new command's reply must stay under 16 lines, or be split as `help` is.
- Add a new command group to the dispatch chain in `ConsoleService::execute()`,
  to the `help` index and groups, and to this document.
- Changing a queue, ring or line limit changes static RAM use; update the
  [Fixed Limits](#fixed-limits) table and
  [Firmware-RAM-Usage.md](Firmware-RAM-Usage.md).
