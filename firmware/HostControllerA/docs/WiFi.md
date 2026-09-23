# WiFi (ST67W611M1)

## Overview

The HostController fetches the astro forecast over WiFi with an ST67W611M1
module. The module runs ST's **T02** firmware: it is only the radio and MAC,
and the TCP/IP stack, DNS and HTTP run on the STM32 in LwIP. The two talk over
SPI1 with DMA, paced by the module's `ST67_RDY` line.

One task, `St67HttpFetchTask`, owns the module. Every fetch joins the network,
gets an address by DHCP, downloads one plain-HTTP response into the caller's
buffer and disconnects. The module and LwIP are started on the first fetch and
then kept running; see [Lifecycle](#lifecycle). The SSID and password come from
the EEPROM (`wifi set`), the server host and path from a compile-time header.

The only regular client is the astro refresh; see
[AstroRefresh.md](AstroRefresh.md). Switch 2 starts a stress batch that is a
bench test, not a product feature; see [Switch 2](#switch-2-stress-batch).

Only the HostController image has WiFi. `User/Src/WiFi` is compiled for the
HostController variant only (`CMakeLists.txt`).

This document replaces the ST67 phase plans, now in
[archive](archive/ST67_Daily_Fetch_Implementation_Plan.md). HTTPS is a separate
plan, [ST67_HTTPS_Implementation_Plan.md](ST67_HTTPS_Implementation_Plan.md),
not started. Rules for generated code are in
[CubeMXCompliance.md](CubeMXCompliance.md).

## Architecture

```text
AstroDataRefreshTask ── FetchSt67Data() ──┐   (caller's thread, waits)
                                          v
St67HttpFetchTask (osPriorityBelowNormal, 2560 B)
  ├─ St67NetworkSession   W6X init, WiFi join, DHCP wait, disconnect
  ├─ St67HttpFetcher      host/path check, DNS, callbacks, result
  │    └─ HttpClient_Get  LwIP socket, GET, header/body parsing
  └─ St67Runtime          shared state and the 4 KB service buffer
        │
LwIP tcpip thread (osPriorityNormal4, 4096 B)   LWIP/Target/lwipopts.h
LwIP netif task   (priority 50, 2048 B)         LWIP/App/lwip_netif.h
W6X modem RX task (priority 47, 2048 B)         w61_driver_config.h / w61_at_common.h
W6X SPI engine    (priority 46, 1536 B)         w61_driver_config.h
        │
SPI1 + DMA1 ch1 (RX) / ch2 (TX), CS, CHIP_EN, ST67_RDY (PA4, EXTI)
        │
ST67W611M1, T02 firmware
```

### Hardware and transport

| Item | Setting |
| --- | --- |
| Module firmware | T02, SDK 2.0.106, reported at start-up as `ST67 module=... sdk=...` |
| Driver | X-CUBE-ST67W61 1.3.0, `ST67_ARCH=W6X_ARCH_T02` (`cmake/stm32cubemx/CMakeLists.txt`) |
| SPI | SPI1 master, HSI 16 MHz / 8 = 2 MHz; DMA1 channel 1 RX, channel 2 TX, high priority, IRQ priority 3 |
| Largest SPI transfer | `W61_MAX_SPI_XFER = 1520` (`ST67W6X_Network_Driver/Target/w61_driver_config.h`) |
| `ST67_RDY` | PA4, EXTI on both edges (EXTI4_15, priority 3) |
| Power save | `W6X_POWER_SAVE_AUTO = 1` (`w6x_config.h`) |

The driver's SPI port (`ST67W6X_Network_Driver/Target/spi_port.c`) is
generated. The rising edge of `ST67_RDY` reaches the driver through
`HAL_GPIO_EXTI_Rising_Callback()` in `User/Src/WiFi/St67SpiReady.cpp`, which
calls `spi_on_txn_data_ready()`. The falling-edge callback belongs to the
switches (`User/Src/Utils/SwitchInput.cpp`).

### Task priorities and stacks

| Task | Priority | Stack | Set in |
| --- | --- | --- | --- |
| `St67HttpFetch` | `osPriorityBelowNormal` (16) | 2560 B, static | `St67HttpFetchTask.cpp` |
| LwIP `tcpip_thread` | 28 (`osPriorityNormal4`) | 4096 B | `LWIP/Target/lwipopts.h` |
| `netif` | 50 | 2048 B | `LWIP/App/lwip_netif.h` (generated) |
| W61 modem RX | 47 | 2048 B | priority in `w61_driver_config.h`, stack default in `w61_at_common.h` |
| `spi_xfer_engine` | 46 | 1536 B | `w61_driver_config.h` |

The driver's two tasks default to 53 and 54, above the display multiplexing
task `DisplayRefresh` (`osPriorityRealtime`, 48). There they held a display
slot for up to 14 ms during WiFi activity, which flashed the whole display, so
they are overridden to 46 and 47. The overrides must live in the `USER CODE
BEGIN EC` block of `w61_driver_config.h`: definitions on the CMake target never
reach the driver, which is compiled in the generated `STM32_Drivers` library.
See [CubeMXCompliance.md](CubeMXCompliance.md#st67-driver-task-settings).

The generated `netif` task runs at 50, above `DisplayRefresh`. It has not been
measured against the display.

The driver and LwIP tasks are created from the 40 000-byte FreeRTOS heap when
the module is first started. See [Firmware-RAM-Usage.md](Firmware-RAM-Usage.md).

The driver's own log output goes through `vLoggingPrintf()`, defined in
`St67HttpFetchTask.cpp`, into `LogService`, truncated to 95 characters.

## Software

| File | Responsibility |
| --- | --- |
| `User/Src/WiFi/St67HttpFetchTask.cpp`, `User/Inc/HostController/St67HttpFetchTask.hpp` | The task. Public API: `FetchSt67Data`, `StartSt67HttpFetchTask`, `SetSt67CredentialSource`, `TriggerSt67ConnectivityCycle`, `LastWifiConnect`. Runs batches and publishes the result. |
| `User/Src/WiFi/St67FetchStatusMap.cpp`, `.../St67FetchStatusMap.hpp` | `fetchStatusForFailure()`: first failed stage to `St67FetchStatus`. Pure. |
| `User/Inc/HostController/St67FetchTypes.hpp` | `St67FetchRequest`, `St67FetchResult`, `St67FetchStatus`, `FetchStage`, the client timeout. |
| `User/Src/WiFi/St67NetworkSession.cpp`, `.../St67NetworkSession.hpp` | `initialize()`, `open()`, `disconnect()`, `stop()`. Credentials, the SSID scan, `LastWifiConnect()`. |
| `User/Src/WiFi/St67ConnectDiagnosis.cpp`, `.../St67ConnectDiagnosis.hpp` | Connect-failure diagnosis: reason code to `WifiConnectResult`, the scan fallback, and the log line for each result. Pure. |
| `User/Src/WiFi/St67NetworkAdapter.cpp`, `.../St67NetworkAdapter.hpp` | Station state from public APIs only: `W6X_WiFi_Station_GetState()` plus the LwIP `NETIF_STA` netif (up, link, IPv4). |
| `User/Src/WiFi/St67HttpFetcher.cpp`, `.../St67HttpFetcher.hpp` | Checks the host and path, resolves DNS, runs one GET, checks `Content-Type`, copies the body and computes its CRC-32. |
| `User/Src/WiFi/St67HttpRules.cpp`, `.../St67HttpRules.hpp` | The fetcher's host/path check (`isValidTarget()`) and `Content-Type` check (`checkContentType()`). Pure. |
| `User/Src/WiFi/HttpClient.cpp`, `User/Inc/HostController/HttpClient.hpp` | `HttpClient_Get()`: a bounded synchronous HTTP/1.1 GET on an LwIP socket. |
| `User/Src/WiFi/HttpResponseParser.cpp`, `.../HttpResponseParser.hpp` | `HttpResponse::`: header end, status line, `Content-Length`, the header buffer and body limits, used by `HttpClient_Get()`. Pure. |
| `User/Inc/HostController/St67Runtime.hpp` | `St67Runtime`: init flags, state, DNS and HTTP results, the first failure, the 4096-byte `httpPayload` buffer, the client request being served. |
| `User/Src/WiFi/St67SpiReady.cpp` | The `ST67_RDY` rising-edge bridge. |
| `User/Src/WiFi/St67ProbeTask.cpp` | Dead code: the raw AT/CWLAP probe from before the driver was used. Compiled, never started. |
| `Appli/App/app_config.h` | Timeouts, limits, lifecycle mode. See [Configuration](#configuration). |
| `Appli/App/app_credentials.h.template` | Template for the git-ignored `app_credentials.h`: HTTP host and path. |

`AppVariant.cpp` calls `SetSt67CredentialSource(&settingsStore)` and then
`StartSt67HttpFetchTask()`. The task waits `APP_ST67_STARTUP_DELAY_MS` (4 s)
after it starts; a request made earlier waits with it.

### Why a User-owned HTTP client

T02 has no HTTP or socket offload in the module: ST's documentation says HTTP
must be built on host LwIP. CubeMX generates `LWIP/App/http_client.c`, which is
still compiled but not called. The project had customized it for request
ownership, cancellation, response limits and cleanup, and a regeneration
overwrote those changes, as most were outside `USER CODE` blocks.
`HttpClient.cpp` replaces it under `User/`, so it survives regeneration. It
still uses the generated `http_client.h` types (`HTTP_connection_t`, the
callbacks and status codes), so the fetcher's callback shape did not change.

## Client API

A client fills an `St67FetchRequest` and calls `FetchSt67Data()` on its own
thread:

```cpp
request_ = {};
request_.buffer = responseBuffer_;         // uint8_t[APP_ST67_HTTP_MAX_RESPONSE_BYTES]
request_.capacity = sizeof(responseBuffer_);
const bool ok = FetchSt67Data(&request_, &onProgress, this);
```

- The call blocks until the WiFi task publishes the result. It waits on thread
  flags `kFetchFlagDone` and `kFetchFlagStage` (bits 24 and 25) of the calling
  thread, which the caller must not use for anything else.
- `onProgress(stage, context)` is called on the caller's thread at every stage
  change and otherwise every **250 ms**, so the caller can animate. It may be
  null.
- The wait is bounded by `kClientFetchTimeoutMs`, **180 s**. The longest run
  seen on the bench was about 2 minutes, the first fetch after boot. On
  timeout the call returns `Timeout`, but the WiFi task cannot be cancelled and
  keeps the request until it finishes: the request and buffer must stay alive,
  and later calls return `Busy` until then.
- Only one request at a time. A second call while one is pending, or while a
  switch 2 batch runs, returns `Busy` at once. Nothing is queued.
- `capacity` must be 1..`APP_ST67_HTTP_MAX_RESPONSE_BYTES` (4096) and `buffer`
  non-null, or the call returns `InvalidArgument`.
- It returns `true` exactly when `result.status` is `Success`.

### Result

| Field | Meaning |
| --- | --- |
| `status` | `St67FetchStatus`, below |
| `httpStatus` | HTTP status code, when a status line was parsed |
| `length` | body bytes in `buffer`; 0 on failure |
| `crc32` | CRC-32 (IEEE, reflected) of those bytes, computed while copying |
| `detail` | the `W6X_Status_t` at the first failure (0 OK, 1 busy, 2 error, 3 timeout); not an HTTP or socket error |
| `responseTick` | `osKernelGetTickCount()` when the response headers arrived; used by the clock sync, see [RTC.md](RTC.md#sync-from-the-api) |

The CRC only checks the hand-off: the caller recomputes it over its buffer to
catch a buffer overwritten between the copy and the parse. It is not an
end-to-end check; the server sends no checksum and TCP's own is all there is.

### Status

The task records the first failing step as a stage name
(`runtime.firstFailureStage`), keeps going through disconnect, then maps it with
`fetchStatusForFailure()`:

| Status | When |
| --- | --- |
| `Success` | no failure |
| `Busy` | a fetch or a switch 2 batch is already running |
| `InvalidArgument` | null request or buffer, capacity 0 or over 4096 |
| `NoCredentials` | no SSID stored (stage `credentials`); checked before the module is powered |
| `DriverFailure` | `W6X_Init()` or `W6X_WiFi_Init()` failed (`w6x-init`, `wifi-init`) |
| `NetworkFailure` | join failed or no DHCP address (`connect`, `connect-state`, `dhcp`); `LastWifiConnect()` says why |
| `ResponseTooLarge` | the body overflowed the caller's buffer |
| `HttpFailure` | everything else: invalid host/path, DNS, TCP, HTTP status outside 2xx, wrong `Content-Type`, a malformed or truncated response, and also `module-info`, `callback-register`, `lwip-init`, `lwip-netif` and disconnect failures |
| `CleanupFailure` | `final-state` (CHIP_EN or RDY still high after `stop()`), or `netif-stop`, which nothing produces any more. A client fetch never calls `stop()`, so clients do not see it. |
| `Timeout` | set by the caller's wait after 180 s, not by the task |

A response whose `Content-Length` is over 4096 is refused by `HttpClient_Get`
before any body is read, and shows as `HttpFailure`, not `ResponseTooLarge`.

### Stages

`FetchStage` is advanced by the task and frozen at the first failure, so after
a failed fetch it names the step that failed, not the cleanup after it.

| Stage | Set when |
| --- | --- |
| `Queued` | accepted, the task has not started |
| `StartingModule` | `initialize()` begins; on later fetches it passes at once, as everything is already up |
| `JoiningWifi` | `W6X_WiFi_Connect()` |
| `GettingIp` | joined, waiting for DHCP |
| `Downloading` | host check, DNS and the GET |
| `Disconnecting` | `W6X_WiFi_Disconnect()` |

The refresh task shows these on the bottom matrix row; see
[AstroRefresh.md](AstroRefresh.md).

## Lifecycle

### One fetch

1. **Initialize, once per boot.** `St67NetworkSession::initialize()` checks
   that an SSID is stored, then, each only if not already done:
   `W6X_Init()` (powers the module up through `CHIP_EN`), `W6X_GetModuleInfo()`
   (logged), `W6X_RegisterAppCb()`, `W6X_WiFi_Init()`, `MX_LWIP_Init()`. It
   then checks that the LwIP station and soft-AP netifs exist. ST requires this
   order: `MX_LWIP_Init()` reads the module's MAC addresses.
2. **Open.** `open()` reads the credentials again, calls `W6X_WiFi_Connect()`
   with one reconnection attempt, checks the station state, logs
   `ST67 connected ssid=... channel=... rssi=...`, then polls every 100 ms for
   up to `APP_ST67_DHCP_TIMEOUT_MS` (15 s) until the station netif is up,
   linked and has an IPv4 address. The password is wiped from the stack
   buffers once passed to the driver.
3. **Fetch.** `St67HttpFetcher::fetch()`; see [HTTP](#http).
4. **Disconnect.** `disconnect()` calls `W6X_WiFi_Disconnect(1)`, where `1`
   restores the station so it does not reconnect on its own, waits up to
   `APP_ST67_DISCONNECT_TIMEOUT_MS` (12 s) for the disconnected event, and
   checks 100 ms later that the link is down and the address cleared. A failed
   join or DHCP also disconnects.

The module, the driver tasks and LwIP stay up between fetches, with the
station disconnected and the module in its automatic power save. Each client
fetch logs `ST67 cycle=<n> result=complete|fault stage=... heap=... min=...
tasks=...` and `ST67 batch-final mode=1 pass=... fail=...`.

### Why persistent

The original plan was to shut the module down (`CHIP_EN` low, about 200 nA)
between daily fetches. That needs a full LwIP teardown, which the generated
code does not provide: `MX_LWIP_DeInit()` and the private netif and timer
cleanup existed only as customizations of generated files, and a CubeMX
regeneration removed them. They were not restored, because they would have to
be written outside `USER CODE` blocks. Even with them, 20 cold restarts lost
about 6 KB of heap; see [Bench results](#bench-results).

Keeping everything initialized passed 100 cycles with a flat heap, so client
fetches always use it. `stop()` exists (`W6X_WiFi_DeInit()`, `W6X_DeInit()`,
then a check that `CHIP_EN` and `ST67_RDY` are low) but deinitializes only the
W6X layers. LwIP stays initialized, and a later `initialize()` re-inits W6X
under the existing netifs. That path has not been tested since the
regeneration.

### Lifecycle modes

A **client fetch always runs as `PersistentStress` for 1 cycle**, whatever
`APP_ST67_LIFECYCLE_MODE` says, and never calls `stop()`.
`APP_ST67_LIFECYCLE_MODE` only chooses what a switch 2 batch does:

| Value | Mode | Switch 2 batch |
| --- | --- | --- |
| 0 | `SINGLE_FULL_SHUTDOWN` | `APP_ST67_COLD_RESTART_STRESS_CYCLES` (20) cycles, each init, join, fetch, disconnect, `stop()`, back to back. Despite the name, not a single cycle. |
| 1 | `PERSISTENT_STRESS` | `APP_ST67_PERSISTENT_STRESS_CYCLES` (100) cycles on one init, `APP_ST67_INTER_CYCLE_DELAY_MS` (1 s) apart, one `stop()` at the end. |
| 2 | `COLD_RESTART_STRESS` | 20 cycles like mode 0, with `APP_ST67_COLD_RESTART_DELAY_MS` (1 s) between them. |
| 3 | `HTTP_PERSISTENT_STRESS` (**default**) | Like mode 1 with `APP_ST67_HTTP_PERSISTENT_STRESS_CYCLES` (100). |

Modes 1 and 3 now differ only in which cycle-count macro they use: both
fetch in every cycle. In the Phase 3 bench runs mode 1 was join, DHCP and
disconnect without HTTP. In the persistent modes a cycle after the first
starts only if `isReady()` confirms the stack is idle and the station
disconnected; otherwise it fails as `persistent-ready` and the batch stops.
A batch also stops at the first failed teardown.

The mode is checked by a `static_assert` and can be overridden with
`-DAPP_ST67_LIFECYCLE_MODE=...`.

## Credentials and endpoint

### WiFi credentials

The SSID and password are stored in the EEPROM with `wifi set <ssid>
[password]` and cleared with `wifi clear`; see [Settings.md](Settings.md) and
[Console.md](Console.md). `St67NetworkSession` copies them from
`Settings::Store` with `copyWifiCredentials()`, under the store's mutex, in
both `initialize()` and `open()`. A `wifi set` therefore takes effect on the
next connect, without a reset. The password is never logged.

With no SSID stored, the fetch fails before the module is powered, as
`NoCredentials`, and logs
`WiFi not configured: no SSID stored. Set one with 'wifi set <ssid> <password>'.`
The refresh scheduler then stops retrying until the next scheduled slot.

`APP_ST67_WIFI_SSID` and `APP_ST67_WIFI_PASSWORD` in `app_config.h` and the
template are left over from before `wifi set` and are not used.

### Server

`APP_ST67_HTTP_HOST` and `APP_ST67_HTTP_PATH` are compile-time values. Copy
`Appli/App/app_credentials.h.template` to `Appli/App/app_credentials.h`,
which git ignores, and fill them in. `app_config.h` includes it when it exists;
without it both are empty and every fetch fails as `HttpFailure` with
`ST67 fetch-config invalid`.

Before each fetch the fetcher rejects:

- a host that is empty, longer than `HTTP_SNI_MAX_SIZE`, or contains `://`,
  `:`, CR or LF. So no scheme and no port: the port is `APP_ST67_HTTP_PORT`
  (80);
- a path that is empty, does not start with `/`, or contains CR or LF.

## HTTP

Plain HTTP only. HTTPS is planned in
[ST67_HTTPS_Implementation_Plan.md](ST67_HTTPS_Implementation_Plan.md).

1. **DNS.** `dns_gethostbyname()` through LwIP, waiting up to
   `APP_ST67_DNS_TIMEOUT_MS` (5 s). The result must be a non-zero IPv4
   address. Failure logs `ST67 dns failed elapsed=...`.
2. **Connect and send.** One TCP socket with `SO_RCVTIMEO` and `SO_SNDTIMEO`
   of `APP_ST67_HTTP_IO_TIMEOUT_MS` (5 s). The request is exactly:

   ```text
   GET <path> HTTP/1.1\r\nHost: <host>\r\nConnection: close\r\n\r\n
   ```

   built in a 512-byte heap buffer. The connect itself has no separate
   timeout beyond LwIP's.
3. **Headers.** Received in 1024-byte reads into a 2048-byte heap buffer,
   `HttpResponse::kHeaderCapacity` in `HttpResponseParser.hpp`. Everything read until the blank line,
   including body bytes that arrive in the same read, must fit in 2048 bytes.
   The status line must be `HTTP/x.y nnn` with `nnn` up to 599.
4. **Checks.** Success needs a 2xx status and a `Content-Type` that starts with
   `APP_ST67_HTTP_EXPECTED_CONTENT_TYPE`, `text/plain; charset=utf-8`. A
   `Content-Length` over 4096 is refused. Header names are matched
   case-sensitively, as `Content-Type:` and `Content-Length:`.
5. **Body.** Copied straight into the caller's buffer (or `httpPayload` for a
   switch 2 batch) while the CRC is updated. With `Content-Length` the body
   must be exactly that long; without it the body ends when the server closes
   the connection. Chunked encoding is not supported: a chunked body would be
   passed on with its chunk markers.
6. **Cleanup.** The socket and both heap buffers are freed on every path, then
   the result callback runs once.

`HttpClient_Get()` is synchronous: it returns when the transfer ends or a
socket operation times out. There is no overall deadline and no cancellation.
`APP_ST67_HTTP_TOTAL_TIMEOUT_MS` is defined but not used; a slow server can
keep the fetch going as long as each read arrives within 5 s, bounded only by
the caller's 180 s.

## Connect failure diagnosis

A failed `W6X_WiFi_Connect()` is classified from the last Wi-Fi reason code the
module reported, so the log and `status` can say what to fix. The mapping and
the messages are in `St67ConnectDiagnosis.cpp`:

| Result | Reason code | Log message (abridged) |
| --- | --- | --- |
| `NetworkNotFound` | 12 `WLAN_FW_SCAN_NO_BSSID_AND_CHANNEL`, or none and the scan found no such SSID | `WiFi network '<ssid>' not found ... Check the SSID; it is case-sensitive.` |
| `WrongPassword` | 7 `DEAUTH_BY_AP_WHEN_CONNECTION`, 8 `4WAY_HANDSHAKE_ERROR_PSK_TIMEOUT_FAILURE` | `... rejected the connection during the password check, which almost always means a wrong password.` |
| `SecurityMismatch` | 2 `AUTHENTICATION_FAILURE`, 3 `AUTH_ALGO_FAILURE`, 17 `NETWORK_SECURITY_NOMATCH` | `... refused authentication. Check the password, and that the network uses WPA2 ...` |
| `NoResponse` | none, and the scan found the SSID | `... is in range but did not answer the connection request.` |
| `NoResponseUnchecked` | none, and the scan could not run | `... did not answer before the connect timeout. Check the SSID ...` |
| `Failed` | any other code, or joined but the station state is not connected | `WiFi '<ssid>' connect failed: <driver's name for the code> (reason <n>).` |
| `DhcpFailed` | joined, no address within 15 s | `WiFi joined '<ssid>' but got no IP address from DHCP.` |
| `NoCredentials` | no SSID stored | see [WiFi credentials](#wifi-credentials) |

A wrong WPA2 password gave reason 7 on the bench; some access points let the
handshake time out instead, giving 8.

A missing network and a silent one both time out with no reason code. To tell
them apart, `open()` then runs one active scan for the SSID, so a hidden
network still answers, and waits up to 8 s for it. The 8000 ms and the 5-result
limit are hardcoded (`kScanTimeoutMs`, `MaxCnt` in `St67NetworkSession.cpp`);
`APP_ST67_SCAN_TIMEOUT_MS` and `APP_ST67_SCAN_MAX_RESULTS` are not used. The
scan logs `WiFi scan for '<ssid>' found <n> access point(s)` or
`... did not complete`.

Every connect, good or bad, is stored as a `WifiConnectSummary`: result,
reason code and its name, SSID, tick, and on success RSSI and channel.
`LastWifiConnect()` returns a copy and is safe from any task. It is read by:

- `status`, which prints for example
  `wifi       'lemo' stored; last connect ok 0d 00:12:03 ago (channel 5, -35 dBm)` or
  `... last connect FAILED ... ago: wrong password`;
- `wifi test` and `wifi set`, which run a refresh and then print one verdict:
  `WiFi test passed: connected to '<ssid>' (channel .., .. dBm) and fetched the forecast.`,
  `WiFi test FAILED for '<ssid>': <result>.`,
  `WiFi test FAILED before connecting: <status>.`, or
  `WiFi test: connected to '<ssid>' ..., but the refresh then failed: <outcome>.`

## Configuration

`Appli/App/app_config.h`:

| Macro | Value | Used |
| --- | --- | --- |
| `APP_ST67_STARTUP_DELAY_MS` | 4000 | yes: task start-up delay |
| `APP_ST67_SCAN_TIMEOUT_MS` | 15000 | **no**; the diagnosis scan uses a hardcoded 8000 ms |
| `APP_ST67_SCAN_MAX_RESULTS` | 20 | **no**; the diagnosis scan uses a hardcoded 5 |
| `APP_ST67_DHCP_TIMEOUT_MS` | 15000 | yes |
| `APP_ST67_DISCONNECT_TIMEOUT_MS` | 12000 | yes |
| `APP_ST67_SHUTDOWN_SETTLING_DELAY_MS` | 100 | yes, in `stop()` |
| `APP_ST67_COLD_RESTART_DELAY_MS` | 1000 | yes, mode 2 batches |
| `APP_ST67_HTTP_PORT` | 80 | yes |
| `APP_ST67_DNS_TIMEOUT_MS` | 5000 | yes |
| `APP_ST67_HTTP_IO_TIMEOUT_MS` | 5000 | yes, socket send and receive timeouts |
| `APP_ST67_HTTP_TOTAL_TIMEOUT_MS` | 15000 | **no**; there is no total deadline |
| `APP_ST67_HTTP_MAX_HEADER_BYTES` | 2048 | **no**; `HttpResponseParser.hpp` hardcodes 2048 |
| `APP_ST67_HTTP_MAX_RESPONSE_BYTES` | 4096 | yes: body limit, `httpPayload` and the refresh task's buffer |
| `APP_ST67_LIFECYCLE_MODE` | 3 | switch 2 batches only |
| `APP_ST67_PERSISTENT_STRESS_CYCLES` | 100 | mode 1 |
| `APP_ST67_COLD_RESTART_STRESS_CYCLES` | 20 | modes 0 and 2 |
| `APP_ST67_HTTP_PERSISTENT_STRESS_CYCLES` | 100 | mode 3 |
| `APP_ST67_INTER_CYCLE_DELAY_MS` | 1000 | modes 1 and 3 |
| `APP_ST67_WIFI_SSID`, `APP_ST67_WIFI_PASSWORD` | `""` | **no**; credentials come from the EEPROM |
| `APP_ST67_HTTP_HOST`, `APP_ST67_HTTP_PATH` | `""` unless set in `app_credentials.h` | yes |
| `APP_ST67_HTTP_EXPECTED_CONTENT_TYPE` | `"text/plain; charset=utf-8"` | yes |

## Switch 2 stress batch

**Bench and test behaviour, not a product feature.** Pressing switch 2 makes
`MainLoopTask` call `TriggerSt67ConnectivityCycle()`, which starts a batch in
`APP_ST67_LIFECYCLE_MODE`: by default 100 join, DHCP, fetch and disconnect
cycles, 1 s apart, then `stop()`, which powers the module down. With the
server answering in a second or two, that takes several minutes.

- It downloads into the task's own 4 KB `httpPayload`, which nothing reads.
- It cannot be cancelled, except by a reset.
- While it runs, every `FetchSt67Data()` returns `Busy`, so scheduled
  refreshes, switch 1, `astro refresh` and `wifi test` all fail.
- A second press during a batch is ignored and logs
  `ST67 batch trigger rejected: active`.
- It ends with `ST67 batch-final mode=3 pass=<n> fail=<n> first=<cycle>
  stage=<stage> status=<w6x> heap=<start>/<end> min=<low> tasks=<start>/<end>`,
  the line to read for a stress result.
- After it, the module is off and the next fetch re-initializes W6X under
  the running LwIP, the path not tested since the regeneration.

`TriggerSt67SmokeTest()` is an unused alias for the same trigger.

## Bench results

All runs used the first board. Details are in the
archived phase plans; the heap figures before 2026-08-26 are from the tree with
the customized LwIP teardown, since removed.

| Phase | Date | Result |
| --- | --- | --- |
| 0.1 Raw AT/CWLAP probe | 2026-08-21 | SPI framing, CS/RDY handshake and multi-frame AT replies work; scans returned 0 to 19 APs. [Daily fetch plan](archive/ST67_Daily_Fetch_Implementation_Plan.md) |
| 1 SPI DMA | 2026-08-21 | 100 DMA init/transfer/deinit cycles, no timeouts or HAL errors, heap flat at 39 080 B. |
| 2 Official driver | 2026-08-21 | `W6X_Init`, WiFi and LwIP init and a scan (20 APs) passed; a HardFault in the scan was fixed by raising the SPI engine stack from 768 to 1536 B. [Phase 2](archive/ST67_Phase_2_Implementation_Plan.md) |
| 3 Join/DHCP/disconnect | 2026-08-22..23 | 100/100 persistent cycles, heap flat at 23 984 B, min 21 552 B. 20/20 cold restarts passed but free heap fell from 33 896 to 27 864 B: not resource-stable. Wrong password, no credentials and AP-off all failed cleanly and recovered. [Phase 3](archive/ST67_Phase_3_Implementation_Plan.md) |
| 4 HTTP fetch | 2026-08-23..24 | First GET: HTTP 200, 83 B. 100/100 `HttpPersistentStress` cycles, heap flat, min 14 680 B. The client-owned buffer hand-off (`FetchSt67Data`) validated, CRC matched. [Phase 4](archive/ST67_Phase_4_Implementation_Plan.md) |
| CubeMX regeneration | 2026-08-26 | With the User-owned `HttpClient`, adapter and RDY bridge: smoke test, 100/100 persistent and 100/100 HTTP persistent cycles, min heap 18 944 B, `St67HttpFetch` 840 B stack left. [CubeMXCompliance.md](CubeMXCompliance.md) |
| Driver priorities | 2026-09-21 | Longest display slot gap during WiFi fell from 14 ms to 8 ms with fetches still succeeding. |
| Connect diagnosis | 2026-09-21 | A wrong WPA2 password reported reason 7 and was classified `WrongPassword`. |

## Open items

- **Power policy.** The module is kept initialized, disconnected and in
  automatic power save between fetches. Its current in that state, during a
  transfer and in `CHIP_EN` shutdown has not been measured, nor have CS, RDY
  and `CHIP_EN` levels or back-powering through GPIO.
- **Cold restart.** Full shutdown and restart lost about 6 KB of heap over 20
  cycles in Phase 3. The cause was not found. Since the regeneration
  `MX_LWIP_DeInit()` and the private LwIP teardown are gone, so `stop()` only
  deinitializes W6X; restarting W6X under a running LwIP (modes 0 and 2, and
  the first fetch after a switch 2 batch) is untested.
- **HTTP error paths untested at runtime:** DNS failure, connection refused,
  timeouts, malformed, truncated, oversized and chunked responses, loss of the
  network mid-transfer. None has been exercised on the bench.
- **No total deadline or cancellation.** `APP_ST67_HTTP_TOTAL_TIMEOUT_MS` is
  unused; a fetch cannot be stopped once started, and a timed-out client
  leaves the task busy until it finishes.
- **Unused configuration.** `APP_ST67_SCAN_*`,
  `APP_ST67_HTTP_TOTAL_TIMEOUT_MS`, `APP_ST67_HTTP_MAX_HEADER_BYTES`,
  `APP_ST67_WIFI_SSID` and `APP_ST67_WIFI_PASSWORD` are defined but not used.
- **Mode 0 runs 20 cycles**, not one, as it falls through to the cold-restart
  count.
- **HTTPS** is not started; see
  [ST67_HTTPS_Implementation_Plan.md](ST67_HTTPS_Implementation_Plan.md).
- **Dead code.** `St67ProbeTask.cpp` is compiled but never started;
  `TriggerSt67SmokeTest()` is never called; the generated
  `LWIP/App/http_client.c` is compiled but not called.
- **Unit tests** cover only the WiFi layer's pure parts: the response
  parser, the host/path and `Content-Type` rules, the connect diagnosis and
  the stage-to-status mapping (see [Testing.md](Testing.md)). The socket,
  DNS and driver calls are not tested natively.
- **The `netif` task at priority 50** is above the display task and has not
  been measured against it.
