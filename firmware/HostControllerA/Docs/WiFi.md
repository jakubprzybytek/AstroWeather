# WiFi (ST67W611M1)

## Overview

The HostController fetches the astro forecast over WiFi with an ST67W611M1
module. The module runs ST's **T01** firmware: TCP/IP, DHCP, DNS and TLS run
inside the module, and the STM32 drives them through the driver's `W6X_Net_*`
socket API. The two talk over SPI1 with DMA, paced by the module's `ST67_RDY`
line. No TCP/IP stack runs on the STM32.

One task, `St67HttpFetchTask`, owns the module. Every fetch joins the network,
waits for the module's DHCP, downloads one HTTPS response into the caller's
buffer and disconnects. The module is started on the first fetch and then kept
running; see [Lifecycle](#lifecycle). The SSID and password come from the
EEPROM (`wifi set`), the server host and path from the EEPROM (`api host`,
`api path`), each falling back to a compile-time default; see [Server](#server).

What has been verified on the bench is summed up in
[Verified on the bench](#verified-on-the-bench).

The only regular client is the astro refresh; see
[AstroRefresh.md](AstroRefresh.md). The task can also run a stress batch, a
bench test that nothing triggers at present; see [Stress batch](#stress-batch).

Only the HostController has WiFi: `User/Src/WiFi` belongs to this project and
is not part of the shared `../Common` code.

Rules for generated code are in
[Development.md](../../Docs/Development.md). The plans and bench records that
led here are in the [archive](archive/README.md).

## Architecture

```text
AstroDataRefreshTask ── FetchSt67Data() ──┐   (caller's thread, waits)
                                          v
St67HttpFetchTask (osPriorityBelowNormal, 4096 B)
  ├─ St67NetworkSession   W6X init, WiFi join, DHCP wait, disconnect
  ├─ St67HttpFetcher      host/path check, DNS, callbacks, result
  │    └─ HttpClient::get W6X_Net socket (TLS or TCP), GET, header/body parsing
  └─ St67Runtime          shared state and the 4 KB service buffer
        │
W6X modem RX task (priority 47, 2048 B)         w61_driver_config.h / w61_at_common.h
W6X SPI engine    (priority 46, 1536 B)         w61_driver_config.h
        │
SPI1 + DMA1 ch1 (RX) / ch2 (TX), CS, CHIP_EN, ST67_RDY (PA4, EXTI)
        │
ST67W611M1, T01 firmware: TCP/IP, DHCP, DNS, TLS
```

### Hardware and transport

| Item | Setting |
| --- | --- |
| Module firmware | T01, SDK 2.0.106 (`st67w611m_mission_t01_v2.0.106.bin`, programmed with `firmware/Bypass`), reported at start-up as `ST67 module=... sdk=...`. `W6X_Init()` refuses a module running the T02 image |
| Driver | X-CUBE-ST67W61 1.3.0, `ST67_ARCH=W6X_ARCH_T01` (`cmake/stm32cubemx/CMakeLists.txt`) |
| SPI | SPI1 master, HSI 16 MHz / 8 = 2 MHz; DMA1 channel 1 RX, channel 2 TX, high priority, IRQ priority 3 |
| Largest SPI transfer | `W61_MAX_SPI_XFER = 1520` (`ST67W6X_Network_Driver/Target/w61_driver_config.h`) |
| `ST67_RDY` | PA4, EXTI on both edges (EXTI4_15, priority 3) |
| Power save | `W6X_POWER_SAVE_AUTO = 1` (`w6x_config.h`) |

The driver's SPI port (`ST67W6X_Network_Driver/Target/spi_port.c`) is
generated. The rising edge of `ST67_RDY` reaches the driver through
`HAL_GPIO_EXTI_Rising_Callback()` in `User/Src/WiFi/St67SpiReady.cpp`, which
calls `spi_on_txn_data_ready()`. The falling-edge callback belongs to the
switches (`../Common/Src/Utils/SwitchInput.cpp`).

### Task priorities and stacks

| Task | Priority | Stack | Set in |
| --- | --- | --- | --- |
| `St67HttpFetch` | `osPriorityBelowNormal` (16) | 4096 B, static; the W6X socket path and the driver's AT trace run on it | `St67HttpFetchTask.cpp` |
| W61 modem RX | 47 | 2048 B | priority in `w61_driver_config.h`, stack default in `w61_at_common.h` |
| `spi_xfer_engine` | 46 | 1536 B | `w61_driver_config.h` |

The driver's defaults, 53 and 54, are overridden to 46 and 47. No task
priority affects the display, which refreshes from the TIM2 interrupt; the
values are kept from when it was a task. The overrides must live in the
`USER CODE BEGIN EC` block of `w61_driver_config.h`: definitions on the CMake
target never reach the driver, which is compiled in the generated
`STM32_Drivers` library. See [Development.md](../../Docs/Development.md).

The driver tasks are created from the 32 000-byte FreeRTOS heap when the module
is first started; the module idle costs about 10.6 KB of it and a fetch about
5 KB more. See [Firmware-RAM-Usage.md](Firmware-RAM-Usage.md).

The driver's own log output goes through `vLoggingPrintf()`, defined in
`St67HttpFetchTask.cpp`, into `LogService`, truncated to 95 characters.

## Software

| File | Responsibility |
| --- | --- |
| `User/Src/WiFi/St67HttpFetchTask.cpp`, `User/Inc/WiFi/St67HttpFetchTask.hpp` | The task. Public API: `FetchSt67Data`, `StartSt67HttpFetchTask`, `SetSt67CredentialSource`, `TriggerSt67ConnectivityCycle`, `LastWifiConnect`. Runs batches and publishes the result. |
| `User/Src/WiFi/St67FetchStatusMap.cpp`, `.../St67FetchStatusMap.hpp` | `fetchStatusForFailure()`: first failed stage to `St67FetchStatus`. Pure. |
| `User/Inc/WiFi/St67FetchTypes.hpp` | `St67FetchRequest`, `St67FetchResult`, `St67FetchStatus`, `FetchStage`, the client timeout. |
| `User/Src/WiFi/St67NetworkSession.cpp`, `.../St67NetworkSession.hpp` | `initialize()`, `open()`, `disconnect()`, `stop()`. Credentials, the SSID scan, `LastWifiConnect()`. |
| `User/Src/WiFi/St67ConnectDiagnosis.cpp`, `.../St67ConnectDiagnosis.hpp` | Connect-failure diagnosis: reason code to `WifiConnectResult`, the scan fallback, and the log line for each result. Pure. |
| `User/Src/WiFi/St67NetworkAdapter.cpp`, `.../St67NetworkAdapter.hpp` | Station state from `W6X_WiFi_Station_GetState()`: disconnected, associated, got an IP. |
| `User/Src/WiFi/St67HttpFetcher.cpp`, `.../St67HttpFetcher.hpp` | Checks the host and path, resolves DNS in the module, runs one GET, checks `Content-Type`, copies the body and computes its CRC-32. |
| `User/Src/WiFi/St67HttpRules.cpp`, `.../St67HttpRules.hpp` | The fetcher's host/path check (`isValidTarget()`) and `Content-Type` check (`checkContentType()`). Pure. |
| `User/Src/WiFi/HttpClient.cpp`, `User/Inc/WiFi/HttpClient.hpp` | `HttpClient::get()`: a bounded synchronous HTTP/1.1 GET on a `W6X_Net` socket, TLS or plain. |
| `User/Src/WiFi/HttpResponseParser.cpp`, `.../HttpResponseParser.hpp` | `HttpResponse::`: header end, status line, `Content-Length`, the header buffer and body limits, used by `HttpClient::get()`. Pure. |
| `User/Src/WiFi/TrustedCa.cpp`, `User/Inc/WiFi/TrustedCa.hpp` | Amazon Root CA 1 as PEM text: the trust anchor for the API's ACM certificate (API Gateway custom domain). |
| `User/Inc/WiFi/St67Runtime.hpp` | `St67Runtime`: init flags, state, the resolved server address, HTTP results, the first failure, the 4096-byte `httpPayload` buffer, the client request being served. |
| `User/Src/WiFi/St67SpiReady.cpp` | The `ST67_RDY` rising-edge bridge. |
| `User/Src/WiFi/St67ProbeTask.cpp` | Dead code: the raw AT/CWLAP probe from before the driver was used. Compiled, never started. |
| `Appli/App/app_config.h` | Timeouts, limits, lifecycle mode. See [Configuration](#configuration). |
| `Appli/App/app_credentials.h.template` | Template for the git-ignored `app_credentials.h`: the built-in HTTP host and path. |
| `User/Src/Astro/ApiTarget.cpp`, `User/Inc/Astro/ApiTarget.hpp` | `resolveApiTarget()`: the saved `api host` / `api path`, each falling back to the built-in value. |

`AstroWeather.cpp` calls `SetSt67CredentialSource(&settingsStore)` and then
`StartSt67HttpFetchTask()`. The task waits `APP_ST67_STARTUP_DELAY_MS` (4 s)
after it starts; a request made earlier waits with it.

### Why a User-owned HTTP client

The driver has its own HTTP client for T01, `W6X_HTTP_Client_Request()` in
`Middlewares/ST/ST67W6X_Network_Driver/Core/w6x_http.c`. It is not used:
it never reads the `timeout` and `max_response_len` it is given, treats only
HTTP 200 as success, does not check `Content-Type`, and runs each request in a
task of its own. `HttpClient.cpp` does the same socket work (`W6X_Net_Socket`,
the TLS options, `W6X_Net_Connect`, `W6X_Net_Send`, `W6X_Net_Recv`) under the
project's bounded parser instead, with its own result codes and no dependency
on generated headers.

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
  stress batch runs, returns `Busy` at once. Nothing is queued.
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
| `Busy` | a fetch or a stress batch is already running |
| `InvalidArgument` | null request or buffer, capacity 0 or over 4096 |
| `NoCredentials` | no SSID stored (stage `credentials`); checked before the module is powered |
| `DriverFailure` | `W6X_Init()`, `W6X_WiFi_Init()` or `W6X_Net_Init()` failed (`w6x-init`, `wifi-init`, `net-init`) |
| `NetworkFailure` | join failed or no DHCP address (`connect`, `connect-state`, `dhcp`); `LastWifiConnect()` says why |
| `ResponseTooLarge` | the body overflowed the caller's buffer |
| `HttpFailure` | everything else: invalid host/path, DNS, TCP connect or TLS handshake, HTTP status outside 2xx, wrong `Content-Type`, a malformed, truncated or overdue response, and also `module-info`, `callback-register` and disconnect failures |
| `CleanupFailure` | `final-state` (CHIP_EN or RDY still high after `stop()`), or `netif-stop`, which nothing produces any more. A client fetch never calls `stop()`, so clients do not see it. |
| `Timeout` | set by the caller's wait after 180 s, not by the task |

A response whose `Content-Length` is over 4096 is refused by `HttpClient::get()`
before any body is read, and shows as `HttpFailure`, not `ResponseTooLarge`.
The log line `ST67 https failed: <reason> status=... bytes=... elapsed=...ms`
names the step: `socket`, `tls-setup`, `connect` (TCP or the TLS handshake),
`send`, `response` or `timeout`.

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
   (logged), `W6X_RegisterAppCb()`, `W6X_WiFi_Init()`, `W6X_Net_Init()`, in
   ST's order.
2. **Open.** `open()` reads the credentials again, calls `W6X_WiFi_Connect()`
   with one reconnection attempt, checks the station state, logs
   `ST67 connected ssid=... channel=... rssi=...`, then waits for up to
   `APP_ST67_DHCP_TIMEOUT_MS` (15 s) until the station state is
   `W6X_WIFI_STATE_STA_GOT_IP`: the module's `GOT_IP` event wakes the wait,
   and the state is polled every 100 ms in case the event is missed. The
   address is logged as `ST67 ip=... gw=...`. The password is wiped from the
   stack buffers once passed to the driver.
3. **Fetch.** `St67HttpFetcher::fetch()`; see [HTTP](#http).
4. **Disconnect.** `disconnect()` calls `W6X_WiFi_Disconnect(1)`, where `1`
   restores the station so it does not reconnect on its own, waits up to
   `APP_ST67_DISCONNECT_TIMEOUT_MS` (12 s) for the disconnected event, and
   checks 100 ms later that the link is down and the address cleared. A failed
   join or DHCP also disconnects.

The module and the driver tasks stay up between fetches, with the station
disconnected and the module in its automatic power save. Each client
fetch logs `ST67 cycle=<n> result=complete|fault stage=... heap=... min=...
tasks=...` and `ST67 batch-final mode=1 pass=... fail=...`.

### Why persistent

Keeping the module and the driver initialized between fetches passed 100
cycles with a flat heap, so client fetches always use it. `stop()` exists
(`W6X_Net_DeInit()`, `W6X_WiFi_DeInit()`, `W6X_DeInit()`, then a check that
`CHIP_EN` and `ST67_RDY` are low) and a later `initialize()` re-inits all
three layers; one restart after a stress batch has been seen to work. Shutting
the module down between fetches (`CHIP_EN` low, about 200 nA) waits until a
cold-restart batch (mode 2) has been measured; see [Open items](#open-items).

### Lifecycle modes

A **client fetch always runs as `PersistentStress` for 1 cycle**, whatever
`APP_ST67_LIFECYCLE_MODE` says, and never calls `stop()`.
`APP_ST67_LIFECYCLE_MODE` only chooses what a stress batch does:

| Value | Mode | Stress batch |
| --- | --- | --- |
| 0 | `SINGLE_FULL_SHUTDOWN` | `APP_ST67_COLD_RESTART_STRESS_CYCLES` (20) cycles, each init, join, fetch, disconnect, `stop()`, back to back. Despite the name, not a single cycle. |
| 1 | `PERSISTENT_STRESS` | `APP_ST67_PERSISTENT_STRESS_CYCLES` (100) cycles on one init, `APP_ST67_INTER_CYCLE_DELAY_MS` (1 s) apart, one `stop()` at the end. |
| 2 | `COLD_RESTART_STRESS` | 20 cycles like mode 0, with `APP_ST67_COLD_RESTART_DELAY_MS` (1 s) between them. |
| 3 | `HTTP_PERSISTENT_STRESS` (**default**) | Like mode 1 with `APP_ST67_HTTP_PERSISTENT_STRESS_CYCLES` (100). |

Modes 1 and 3 differ only in which cycle-count macro they use: both
fetch in every cycle. In the persistent modes a cycle after the first
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

The host, path and device key are set on the console with `api host <host>`,
`api path <path>` and `api key <key>`, saved in the EEPROM (tags `ApiHost`,
`ApiPath`, `ApiKey`; see [Settings.md](Settings.md#tag-registry)), and read at
the start of every fetch, so a change applies from the next one. The API wants
the path `/device/astro/<configurationId>` and the stage's key; the fetch
appends the key as `?key=<key>` (`St67HttpRules::formatRequestPath`), because
the T01 driver cannot add a request header. Without a key the server answers
401, with a wrong one 403
(`sst/docs/architecture.md#access-control`). `api show` and the `api` line of
`status` show what is used; see [Console.md](Console.md#api).

Each part that is not saved falls back to its built-in value,
`APP_ST67_HTTP_HOST` / `APP_ST67_HTTP_PATH` / `APP_ST67_HTTP_KEY` (an empty
key sends none). Those come from
`Appli/App/app_credentials.h`, a git-ignored copy of
`Appli/App/app_credentials.h.template`, which `app_config.h` includes when it
exists. With neither a saved nor a built-in value, every fetch fails as
`HttpFailure` with `ST67 fetch-config invalid`. The port is
`APP_ST67_HTTPS_PORT` (443), or `APP_ST67_HTTP_PORT` (80) when the firmware is
built with `-DAPP_ST67_HTTP_USE_TLS=0` for bench diagnostics.

Each fetch logs the target at `Debug` level: `ST67 fetch https://<host><path>
key=<set>|<none> (saved|built-in)`, where `saved` means at least one part was
saved. The key itself is never logged.

Before each fetch the fetcher rejects:

- a host that is empty, longer than `W6X_NET_SNI_MAX_SIZE` (64, the module's
  SNI limit), or contains `://`, `:`, `/`, a space, tab, CR or LF. So no
  scheme, no port and no path;
- a path that is empty, does not start with `/`, or contains a space, tab, CR
  or LF.

The `api host` and `api path` commands apply the same rules before saving.

## HTTP

HTTPS by default, with TLS in the module; plain HTTP is a build option for the
bench (`APP_ST67_HTTP_USE_TLS=0`). The plan that led here is archived in
[ST67_HTTPS_Implementation_Plan.md](archive/ST67_HTTPS_Implementation_Plan.md).

1. **DNS.** `W6X_Net_ResolveHostAddress()`: the module's resolver, synchronous,
   bounded by the driver's own timeout (`APP_ST67_DNS_TIMEOUT_MS` is not
   involved). The result must be a non-zero IPv4 address. Failure logs
   `ST67 dns failed elapsed=...`.
2. **Socket and TLS.** `W6X_Net_Socket()` with `IPPROTO_TLS_1_2`, or
   `IPPROTO_TCP` for plain HTTP, with `SO_RCVTIMEO` and `SO_SNDTIMEO` of
   `APP_ST67_HTTP_IO_TIMEOUT_MS` (5 s). For TLS the client then uploads
   Amazon Root CA 1 (`TrustedCa`) into the module's file system as
   `AmazonRootCA1.pem` with `W6X_Net_TLS_Credential_AddByContent()`, selects
   it with `TLS_SEC_TAG_LIST`, sets the SNI (`TLS_HOSTNAME`) to the host and
   ALPN to `http/1.1`. With a CA set the driver asks the module for server
   authentication (`AT+CIPSSLCCONF` auth mode 2). Before the upload the
   driver lists the module's file system (`AT+FS=0,5`) and compares an
   existing copy by size and content, so the module must carry the slim
   LittleFS image from `firmware/Bypass/tools/Build-LittleFS.sh`: with ST's
   stock image the listing alone takes longer than the driver's 2 s command
   timeout and every fetch fails as `tls-setup`. The host-side credential
   tag is released when the socket closes; the file stays on the module.
   After a module restart the module reports the file's size rounded up to
   256 bytes, so the first fetch of each boot rewrites it (about 3 s); later
   fetches find it unchanged. The module enforces chain and hostname
   verification and fails closed; what it does about validity dates is still
   to be established.
3. **Connect and send.** `W6X_Net_Connect()` does the TCP connect and, on a
   TLS socket, the handshake; the driver's own timeout bounds it. The request
   is exactly:

   ```text
   GET <path> HTTP/1.1\r\nHost: <host>\r\nConnection: close\r\n\r\n
   ```

   built in a 512-byte heap buffer.
4. **Headers.** Received in 1024-byte reads into a 2048-byte heap buffer,
   `HttpResponse::kHeaderCapacity` in `HttpResponseParser.hpp`. Everything read until the blank line,
   including body bytes that arrive in the same read, must fit in 2048 bytes.
   The status line must be `HTTP/x.y nnn` with `nnn` up to 599.
5. **Checks.** Success needs a 2xx status and a `Content-Type` that starts with
   `APP_ST67_HTTP_EXPECTED_CONTENT_TYPE`, `text/plain; charset=utf-8`. A
   `Content-Length` over 4096 is refused. Header names are matched
   case-sensitively, as `Content-Type:` and `Content-Length:`.
6. **Body.** Copied straight into the caller's buffer (or `httpPayload` for a
   stress batch) while the CRC is updated. With `Content-Length` the body
   must be exactly that long; without it the body ends when the server closes
   the connection. Chunked encoding is not supported: a chunked body would be
   passed on with its chunk markers.
7. **Cleanup.** The socket, the uploaded certificate and both heap buffers are
   released on every path, then the result callback runs once.

`HttpClient::get()` is synchronous. Each read waits up to the 5 s socket
timeout, and the whole response, from connect to the last body byte, must
arrive within `APP_ST67_HTTP_TOTAL_TIMEOUT_MS` (15 s), or the fetch fails as
`timeout`. There is no cancellation. Two driver traits shape the receive loop:
`W6X_Net_Recv()` returns 0 on a read timeout, and −1 both when the peer closed
and when the socket failed, so a close-delimited body (no `Content-Length`)
cannot be told from a reset; this API always sends `Content-Length`.

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
| `APP_ST67_HTTP_USE_TLS` | 1 | yes: HTTPS; `0` builds the plain-HTTP bench variant |
| `APP_ST67_HTTPS_PORT` | 443 | yes, with TLS |
| `APP_ST67_HTTP_PORT` | 80 | yes, without TLS |
| `APP_ST67_DNS_TIMEOUT_MS` | 5000 | **no**; the module's resolver has its own timeout |
| `APP_ST67_HTTP_IO_TIMEOUT_MS` | 5000 | yes, socket send and receive timeouts |
| `APP_ST67_HTTP_TOTAL_TIMEOUT_MS` | 15000 | yes: connect, handshake and response together |
| `APP_ST67_HTTP_MAX_HEADER_BYTES` | 2048 | **no**; `HttpResponseParser.hpp` hardcodes 2048 |
| `APP_ST67_HTTP_MAX_RESPONSE_BYTES` | 4096 | yes: body limit, `httpPayload` and the refresh task's buffer |
| `APP_ST67_LIFECYCLE_MODE` | 3 | stress batches only |
| `APP_ST67_PERSISTENT_STRESS_CYCLES` | 100 | mode 1 |
| `APP_ST67_COLD_RESTART_STRESS_CYCLES` | 20 | modes 0 and 2 |
| `APP_ST67_HTTP_PERSISTENT_STRESS_CYCLES` | 100 | mode 3 |
| `APP_ST67_INTER_CYCLE_DELAY_MS` | 1000 | modes 1 and 3 |
| `APP_ST67_WIFI_SSID`, `APP_ST67_WIFI_PASSWORD` | `""` | **no**; credentials come from the EEPROM |
| `APP_ST67_HTTP_HOST`, `APP_ST67_HTTP_PATH` | `""` unless set in `app_credentials.h`; the fallback for `api host` / `api path` | yes |
| `APP_ST67_HTTP_EXPECTED_CONTENT_TYPE` | `"text/plain; charset=utf-8"` | yes |

### Driver AT trace

`W61_AT_LOG_ENABLE` in `ST67W6X_Network_Driver/Target/w61_driver_config.h`
(a generated line, 0 in the repository) makes the driver log every AT command
and reply as `AT> ...` / `AT< ...` Debug lines. It is the fastest way to see
what the module answers, and it found the file-listing timeout above. Two
cautions: it logs `AT+CWJAP="<ssid>","<password>"`, so the Wi-Fi password
goes into the console and any capture of it, and it must never be committed
on; and it runs on the calling task's stack, which is why the fetch task has
4096 B.

## Stress batch

**Bench and test behaviour, not a product feature.** The console command
`wifi stress` ([Console.md](Console.md#wifi)) calls
`TriggerSt67ConnectivityCycle()`; nothing else calls it. The trigger starts a batch in `APP_ST67_LIFECYCLE_MODE`: by default 100 join,
DHCP, fetch and disconnect cycles, 1 s apart, then `stop()`, which powers the
module down. At about 13 s per HTTPS cycle that takes about 25 minutes.

- It downloads into the task's own 4 KB `httpPayload`, which nothing reads.
- It cannot be cancelled, except by a reset.
- While it runs, every `FetchSt67Data()` returns `Busy`, so scheduled
  refreshes, switch 1, `astro refresh` and `wifi test` all fail.
- A second trigger during a batch is ignored and logs
  `ST67 batch trigger rejected: active`.
- It ends with `ST67 batch-final mode=3 pass=<n> fail=<n> first=<cycle>
  stage=<stage> status=<w6x> heap=<start>/<end> min=<low> tasks=<start>/<end>`,
  the line to read for a stress result.
- After it, the module is off and the next fetch re-initializes the driver.

`TriggerSt67SmokeTest()` is an unused alias for the same trigger.

## Verified on the bench

On the first board, with the module on the T01 image and the API behind its
API Gateway custom domain:

- **HTTPS fetches** of `/device/astro/<id>?key=...`: HTTP 200, the full
  payload (about 1.9 KB), CRC valid, parse OK. DNS takes about 0.3 s, the
  TLS handshake about 1 s, the certificate upload about 3 s on the first fetch
  of a boot. A missing key gets 401 from the server, a wrong one 403.
- **Certificates.** With a bench build trusting ISRG Root X1
  (`-DAPP_ST67_TLS_BENCH_ANCHOR_ISRG=ON`, badssl.com hosts; see
  [Testing.md](Testing.md)), a wrong CA, a hostname mismatch, an untrusted
  root, a self-signed certificate and a chain to another root are all refused
  in the handshake; the positive controls complete with HTTP 200. Expired
  certificates have not been tried.
- **Stress.** `wifi stress`: 100/100 HTTPS cycles in about 21 minutes, free
  heap the same after every cycle, 13 tasks; with the FreeRTOS heap at
  32 000 B, 99/100 (one transient connect failure, recovered) and `heapMin`
  16 336 B. After `stop()`, a `wifi test` restarted the module and fetched.
- **Connect diagnosis.** A wrong WPA2 password reports reason 7 and is
  classified `WrongPassword`.
- **Heap.** With the module up and idle, about 21.4 KB of the 32 000 B heap is
  free; a fetch takes the low-water mark to about 16.5 KB.

The bench record, with dates and figures, is in
[archive/WiFi_Bench_Results.md](archive/WiFi_Bench_Results.md).

## Open items

- **Power policy.** The module is kept initialized, disconnected and in
  automatic power save between fetches. Its current in that state, during a
  transfer and in `CHIP_EN` shutdown has not been measured, nor have CS, RDY
  and `CHIP_EN` levels or back-powering through GPIO.
- **Cold restart.** Only one restart after `stop()` has been run; the
  cold-restart batches (modes 0 and 2) have not.
- **Certificate validity dates.** Whether the module checks them is not
  established.
- **HTTP error paths untested at runtime:** DNS failure, connection refused,
  timeouts, malformed, truncated, oversized and chunked responses, loss of the
  network mid-transfer.
- **Small heap retentions.** A cycle that fails in the TLS connect keeps 32 B
  of heap for good, and every `stop()`/re-initialisation keeps about 240 to
  270 B. Successful cycles keep nothing. Not located in the driver yet.
- **No cancellation.** A fetch cannot be stopped once started, and a
  timed-out client leaves the task busy until it finishes; the 15 s total
  deadline bounds the HTTP part only.
- **Unused configuration.** `APP_ST67_SCAN_*`, `APP_ST67_DNS_TIMEOUT_MS`,
  `APP_ST67_HTTP_MAX_HEADER_BYTES`, `APP_ST67_WIFI_SSID` and
  `APP_ST67_WIFI_PASSWORD` are defined but not used.
- **Mode 0 runs 20 cycles**, not one, as it falls through to the cold-restart
  count.
- **Dead code.** `St67ProbeTask.cpp` is compiled but never started;
  `TriggerSt67SmokeTest()` is an unused alias of the `wifi stress` trigger.
- **Unit tests** cover only the WiFi layer's pure parts: the response
  parser, the host/path and `Content-Type` rules, the connect diagnosis and
  the stage-to-status mapping (see [Testing.md](Testing.md)). The socket,
  DNS and driver calls are not tested natively.
