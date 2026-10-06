# ST67 HTTPS Implementation Plan

**Status: implemented with Route B (TLS in the module, T01), on `main` since
2026-10-04.** The host runs T01 against `W6X_Net` with HTTPS on by default,
the module carries `mission_t01_v2.0.106` with a LittleFS holding only
Amazon Root CA 1, and fetches from `https://api.int.astroweather.albedoonline.com`
succeed with the module enforcing chain and hostname verification. The
phases as built are sections 4 to 8, the bench evidence is the
[bench record](#bench-record-2026-10-04-route-b), and section 11 lists what
is still open: the expired-certificate case, the transport fault cases, an
alternating failure batch, the fetch-task stack high-water mark and a fake
socket layer for native tests of `HttpClient::get()`. The Wi-Fi stack as
built is described in [WiFi.md](../WiFi.md).

There are two ways to get TLS on this board, and the first decision is which
one to take; see [Two routes](#2-two-routes). Route A keeps the current T02
architecture and runs mbedTLS on the STM32. Route B switches the module to
ST's T01 architecture, where the module runs TCP/IP and TLS itself. As of
2026-10-02 Route B was the preferred candidate, to be tried first; the T02
choice was never backed by a requirement (see [History](#history-of-the-architecture-choice)).
Route B passed its spike and was adopted on 2026-10-04. Sections 4 to 8
describe it as built; section 3 lists the decisions both routes need; the
Route A phases are kept as [Appendix A](#appendix-a-route-a-phases-mbedtls-on-the-host-t02-not-taken).

## 1. Goal

Add authenticated HTTPS GET support to the existing ST67 daily-fetch path while
preserving plain HTTP for bench diagnostics and keeping all application behavior
outside generated CubeMX sources.

The production path must provide:

- TLS over the host-side LwIP socket used by the T02 architecture;
- server certificate-chain and hostname verification;
- bounded connect, handshake, send, receive, and total-request timeouts;
- the same HTTP status, content type, framing, and response-size checks as the
  current plain HTTP client;
- deterministic cleanup after every success and failure;
- no progressive heap, task, pbuf, or socket loss over repeated requests.

HTTPS is not accepted if certificate verification is disabled or optional.

## 2. Two Routes

| | Route A: mbedTLS on the host (T02) | Route B: TLS in the module (T01) |
| --- | --- | --- |
| Module firmware | `st67w611m_mission_t02_v2.0.106.bin`, as flashed today | `st67w611m_mission_t01_v2.0.106.bin`; `W6X_Init()` refuses a mismatch, so the host build and the module image change together |
| TCP/IP, DNS | LwIP on the STM32 (today) | Inside the module; host calls `W6X_Net_*` |
| TLS | mbedTLS 3.6.4 from X-CUBE-ST67W61 1.3.0, compiled into the STM32 image | Inside the module: `W6X_Net_Socket(..., "SSL")`, `W6X_Net_TLS_Credential_AddByContent()`, or the one-call `W6X_HTTP_Client_Request()` with `https_certificate` and `server_name` (SNI) |
| HTTP | `User/Src/WiFi/HttpClient.cpp` over a transport (to be refactored) | Module's HTTP client via `W6X_HTTP_Client_Request()`, or `HttpClient.cpp` over a `W6X_Net` "SSL" socket |
| Entropy | **Open problem**: STM32G0B1 has no RNG; ST's own example seeds mbedTLS from the U5 RNG (`MBEDTLS/Target/hardware_rng.c`) | Module's hardware RNG; nothing to do on the host |
| Certificate time | **Open problem**: RTC is unset after power loss, holds local time; see [Certificate time](#certificate-time) | Module has SNTP (`W6X_Net_SNTP_*`); the host RTC is not involved |
| Host RAM | Needs roughly 20 to 30 KiB more heap with trimmed TLS buffers, 45 KiB or more with mbedTLS defaults, plus 4 to 6 KiB more `St67HttpFetch` stack. Static reservation is already 91.5% (`docs/Firmware-RAM-Usage.md`) | Frees LwIP: 33.5 KiB heap, about 11 KiB pools, the 4 KiB `tcpip_thread` and 2 KiB netif stacks. ST's stated minimum for the driver is 48 KiB RAM |
| Host flash | Release 197 KiB of 512 KiB is fine. Debug is `-O0` and already 391 KiB; mbedTLS at `-O0` will likely not fit unless it is built with `-Os` separately | Shrinks |
| Handshake CPU | Cortex-M0+ at 64 MHz, no crypto hardware: several seconds per handshake; raise `APP_ST67_HTTP_TOTAL_TIMEOUT_MS` | In the module |
| Host code change | Transport refactor of `HttpClient.cpp`, new `TlsTransport`, trust store, entropy and time policies, tests | Rewrite `St67NetworkSession` and `St67HttpFetcher`/`HttpClient` against `W6X_Net`/`W6X_HTTP` instead of LwIP BSD sockets; remove LwIP from CubeMX |
| What stays User-verifiable | Everything: the verification policy is in our code and host-testable | Verification policy lives in the module firmware; what it checks (chain, hostname, validity dates) must be established on the bench against bad certificates |
| Known risks | Fit. Entropy source needs an analysis. ST's example disables the validity-date check (`MBEDTLS_HAVE_TIME` undefined) | ST release notes for 1.3.0, T01: "SSL sockets support a limited amount of algorithms, handshake might fail if unsupported algorithm is used by the server" and "SSL sockets has specific internal buffer configurations, this might result in failure when server doesn't acknowledge those changes". Also DHCP/static-IP caveats. Must be proven against CloudFront |

The API is CloudFront with an ACM certificate (`sst/docs/architecture.md`), so
the server side is the same for both routes. Observed with `openssl s_client`
on 2026-10-03 against `api.int.astroweather.albedoonline.com` (only the `int`
stage is deployed at present): leaf `CN=api.int.astroweather.albedoonline.com`,
RSA 2048, valid 2026-09-18 to 2027-04-03, issued by `Amazon RSA 2048 M04`,
which is issued by **Amazon Root CA 1** (the server also sends the Starfield
G2 cross-certificate, which a client trusting Amazon Root CA 1 never needs).
OpenSSL negotiated TLS 1.3 with X25519 and `TLS_AES_128_GCM_SHA256`; the
module offers TLS 1.2, which CloudFront's default security policy also
accepts, so the 1.2 handshake and cipher choice are a bench item.

### Decision order

1. **Route B spike first.** Flash one module with the T01 image, build a
   minimal T01 host (no LwIP), and fetch from
   `https://api.int.astroweather.albedoonline.com` with Amazon Root CA 1 as
   the CA. Then run it against a wrong CA, a hostname mismatch
   and an expired certificate (a local test server is enough) and record what
   the module rejects. Record `heapFree`/`heapMin` and the image size.
2. If the module verifies the chain and hostname and fails closed, adopt
   Route B and write its plan in full; the Route A phases remain as the
   fallback. **Outcome, 2026-10-04:** adopted; sections 4 to 8 are the Route B
   phases and Appendix A holds Route A.
3. If the module does not verify, or cannot talk to CloudFront, fall back to
   Route A with the decisions in section 3.

### History of the architecture choice

T02 was chosen in August 2026 without a requirement behind it; the archived
plans call it "required" only because the LwIP-on-host examples were being
followed. Nothing in the product needs the TCP/IP stack on the host. The
module in the field already runs T02 2.0.106 and must be re-flashed for
Route B. The programming path is the `firmware/Bypass` project: a bridge
firmware for the same STM32 that holds the module in its ROM bootloader and
lets ST's `QConn_Flash_Cmd` program it over USB (`firmware/Bypass/README.md`,
`docs/ST67_Bypass_Maintainer_Notes.md`). It was validated on 2026-08-18 by
programming the T02 2.0.106 image and reading all 4 MiB back byte-exact.

## 2a. Current State and Constraints (Route A)

The active path is:

```text
St67HttpFetchTask
  -> St67NetworkSession (Wi-Fi association and DHCP)
  -> St67HttpFetcher (DNS and request/result ownership)
  -> HttpClient_Get (LwIP BSD socket and HTTP/1.1 parsing)
  -> ST67 T02 netif over SPI
```

Relevant ownership boundaries are:

- `User/Src/WiFi/St67HttpFetchTask.cpp`: serializes requests and owns lifecycle.
- `User/Src/WiFi/St67HttpFetcher.cpp`: validates endpoint configuration,
  resolves DNS, prepares callbacks, and evaluates the final HTTP result.
- `User/Src/WiFi/HttpClient.cpp`: owns the current synchronous TCP socket,
  request write, bounded response parsing, and cleanup.
- `Appli/App/app_config.h`: non-secret limits and endpoint defaults.
- `Appli/App/app_credentials.h.template`: built-in fallback host and path. Wi-Fi
  credentials are stored in the EEPROM with `wifi set`; see [Settings.md](../Settings.md).

The build selects `ST67_ARCH=W6X_ARCH_T02`. Consequently, TLS runs on the
STM32 host above LwIP; the ST67 HTTP/network offload APIs are not the transport
for this feature.

The generated tree contains `LWIP/App/altls_mbedtls.c`, and the generated HTTP
client has a port-443 branch. That branch is not production-ready:

- no mbedTLS sources or include paths are currently linked;
- `MBEDTLS_CONFIG_FILE` is not defined;
- no CA certificate is configured;
- client verification is `MBEDTLS_SSL_VERIFY_OPTIONAL`;
- TLS configuration and connection pointers are file-static;
- handshake and close operations do not have an independent total deadline.

Do not route the application back through the generated HTTP client or patch
its non-USER sections. Use it only as an API reference.

mbedTLS does not need to be fetched from its upstream repository: X-CUBE-ST67W61
1.3.0 ships mbedTLS 3.6.4 as a CubeMX component (pack class **Security**,
component **mbedTLS**), offered only when LwIP and the T02 architecture are
selected, which is this project's configuration. ST's `ST67W6X_FOTA_LWIP`
example (NUCLEO-U575ZI-Q) shows the generated result: `MBEDTLS/App/mbedtls_config.h`,
`MBEDTLS/App/mbedtls.c` and `MBEDTLS/Target/hardware_rng.c`, and the `.ioc`
keys `SecurityJjmbedTLS_Checked=true` and `mbedTLSCcSecurityJjmbedTLS=true`.
The older 1.1.0 pack also installed on the development PC has no mbedTLS
component. The component is not available in T01, where TLS is in the module.

Memory is a primary constraint. The STM32G0B1 has 144 KiB RAM, the current
image statically reserves about 90%, and the 40 KiB FreeRTOS heap is shared by
ST67 tasks, LwIP, application tasks, and future TLS allocations. TLS sizing
must be measured, not inferred from a successful link.

## 2b. Route B Outline (TLS in the Module, T01)

The outline written before the spike, kept for the reasoning; the phases as
built are sections 4 to 8, and where they differ they win (the fetch kept
`HttpClient.cpp`'s parser over a `W6X_Net` socket, and the CA is uploaded by
the driver into the module's LittleFS). Sections
[Spike preconditions](#spike-preconditions) and
[What the T01 driver does](#what-the-t01-driver-does) record what was checked
on 2026-10-03; the shape of the implementation was:

1. **Module and CubeMX.** Flash the T01 mission image. In CubeMX switch the
   X-CUBE-ST67W61 *ST67 Architecture* to T01 and untick LwIP; `ST67_ARCH`
   becomes `W6X_ARCH_T01`. Regenerate; `LWIP/` and `altls_mbedtls.c` go away.
   Record flash, `.data` and `.bss` against the plain-HTTP baseline.
2. **Network session.** `St67NetworkSession` keeps `W6X_WiFi_*` for join and
   disconnect; DHCP and DNS move to the module (`W6X_Net_ResolveHostAddress()`).
3. **Fetch.** Either call `W6X_HTTP_Client_Request()` with `server_name`,
   `https_certificate`, `timeout`, `max_response_len` and the receive/result
   callbacks, or keep `HttpClient.cpp`'s parser over a `W6X_Net` "SSL" socket
   with `TLS_SEC_TAG_LIST`, `TLS_HOSTNAME` and `TLS_ALPN_LIST` socket options.
   The second keeps the existing bounded parser and status/content-type checks;
   the first is less code. Decide after the spike shows what the module's HTTP
   client enforces.
4. **Trust material.** Amazon Root CA 1 as a const PEM in a User-owned
   source (ST's example passes PEM text through `W6X_Certificate_t::content`);
   the certificate is public data. No host LittleFS is needed for a single CA.
5. **Time.** The module checks validity dates against its own SNTP time, if it
   checks them at all. The host RTC continues to be set from the `time` record
   (see [RTC.md](../RTC.md)) and the generated SNTP client stays off.
6. **Failure mapping and tests.** Map `W6X_Status_t`/HTTP result codes to the
   existing fetch results; the HTTP parser tests apply only if the parser is
   kept. Bench validation reuses the section 8 cases, with the certificate
   failure cases being the ones that establish what the module verifies.

### Spike preconditions

Checked 2026-10-03. Everything the spike needs is on disk; the order below is
the one to follow, because `W6X_Init()` refuses a host/module architecture
mismatch and the device cannot fetch until both sides agree.

1. **Host first.** Switch CubeMX to T01, regenerate, and get the T01 host to
   build and link before the module is touched. This finds fit and
   regeneration problems with no hardware at risk, and leaves the T02 build
   in git to restore the field configuration.
2. **Bypass.** `firmware/Bypass` has only a `Debug-Manufacture` build; build
   a `*-Bootloader` preset (toolchain PATH recipe in its maintainer notes),
   flash it to the STM32 over ST-LINK, then
   `./tools/Query-ST67.sh --port COM4` (non-destructive),
   `./tools/Program-ST67.sh --port COM4 --profile MissionT01` (dry run, checks
   every input) and the same with `--force`. `Bypass.ioc` names an
   `STM32G0B0CETx` while the board carries an `STM32G0B1CET6`; the pins are
   identical and the T02 flash worked, so this is a documentation
   inconsistency, not a blocker.
3. **Assets.** The vendored pack `firmware/Bypass/External/x-cube-st67w61` is
   1.3.0 (git-ignored, present on the development PC). Its
   `st67w611m_mission_t01_v2.0.106.bin` is byte-identical to the copy in the
   installed CubeMX pack, so the module image and the host driver come from
   the same release. Both the `mission_t01` and the `mission_t02` flash
   configurations also write ST's `LittleFS/littlefs/littlefs.bin` (sample
   certificates) to the module at `0x378000`, so the module already carries
   that partition today.
4. **Flash the T01 host** over ST-LINK and run the spike. Keep the T02 host
   image and the `MissionT02` profile at hand to roll back.

### What the T01 driver does

Verified in X-CUBE-ST67W61 1.3.0, `Middlewares/ST/ST67W6X_Network_Driver`.
The plan's requirements in [Goal](#1-goal) map onto it as follows.

| Topic | Driver behaviour | Consequence |
| --- | --- | --- |
| Verification mode | With a CA set, `w6x_net.c` sends `AuthMode = 2` ("Server Only") in `AT+CIPSSLCCONF`, so the module is asked to verify the chain | Chain verification is requested; whether it is enforced is a bench case |
| Hostname | SNI goes to the module in `AT+CIPSSLCSNI`; nothing on the host compares the name with the leaf | Whether the module checks the hostname is **unknown**; the mismatch bench case decides Route B |
| TLS version, ALPN | Port 443 gives an `IPPROTO_TLS_1_2` socket; ALPN is hardcoded `"http/1.1,http/1.2"` | Matches the protocol profile |
| `settings.timeout`, `settings.max_response_len` | **Never read** in `w6x_http.c` | No total deadline and no response cap from the driver. The 4096-byte limit and the request deadline must be enforced in our `recv_fn` (return a negative value to abort), or the existing parser is kept over a `W6X_Net` SSL socket |
| Status | Only HTTP 200 is a success; other codes reach `result_fn` without a body | Equivalent to today's 2xx rule for this API |
| `Content-Type` | Not checked; `headers_done_fn` receives the raw header buffer | Run `checkContentType()` there |
| Body end | `Content-Length` when present, otherwise a trailing `\r\n\r\n` heuristic; parsing is `strstr`-based | Fine for this text payload; not close-delimited |
| Resources | One `xTaskCreate` worker per request (`W6X_HTTP_CLIENT_THREAD_STACK_SIZE`, 1536 B), a malloc'd receive buffer (`W6X_HTTP_CLIENT_DATA_RECV_SIZE`: 1024 in the config template, 4096 in the header default) and a malloc'd request string | Measure heap minima across repeated requests as in section 8 |
| Certificate | `W6X_Net_TLS_Credential_AddByContent()` writes the PEM into the **module's** LittleFS before each request and `Credential_Delete` removes it after. Needs `LFS_ENABLE == 0` on our host (no host LittleFS) | One flash write per daily fetch; acceptable. Alternative: bake the CA into `littlefs.bin` with `LittleFS/mklfs/mklfs.exe` and use `AddByName` |
| Session | T01 needs `W6X_Net_Init()` after `W6X_WiFi_Init()`; DHCP runs in the module (`W6X_NET_DHCP 1`); IP readiness is the `W6X_WIFI_EVT_GOT_IP_ID` event or station state `W6X_WIFI_STATE_STA_GOT_IP` | `St67NetworkAdapter` and the DHCP wait in `St67NetworkSession` are rewritten against these |
| Generated types | `User/Src/WiFi/HttpClient.cpp` uses `HTTP_connection_t` and the callbacks from the generated `LWIP/App/http_client.h`, which T01 removes | Move to `W6X_HTTP_connection_t` and the `W6X_HTTP_*` callbacks, or drop `HttpClient.cpp` |
| CubeMX | T01 is the pack's default variant with no condition; `w6x_http.c` is in `ServiceAPI` and already compiled. Our `w6x_config.h` holds only `W6X_POWER_SAVE_AUTO` and `W6X_CLOCK_MODE`; regeneration adds the `W6X_NET_*` and `W6X_HTTP_*` group from `Conf/w6x_config_template.h` | Review the generated values, especially the receive buffer sizes, against the RAM budget |
| Time | `W6X_Net_SNTP_*` exists but nothing enables it by default | Whether the module checks validity dates at all is the "expired certificate" bench case |
| Release notes | 1.3.0, T01 known limitations: "SSL sockets support a limited amount of algorithms, handshake might fail if unsupported algorithm is used by the server" and "SSL sockets has specific internal buffer configurations, this might result in failure when server doesn't acknowledge those changes" | The spike must run against the real CloudFront endpoint |

## 3. Decisions Required Before Implementation

Record these decisions in this document before enabling the production path.
The trust model and protocol profile apply to both routes; certificate time and
entropy are Route A problems, as the module solves them in Route B.

### Trust model

Choose one:

1. **Pinned private CA** for an endpoint under project control. This is the
   smallest trust store, but it is not available here: the production endpoint
   is CloudFront with an ACM-issued certificate, so the issuer is Amazon's.
2. **Curated public roots** containing only roots needed by the production
   endpoint: Amazon Root CA 1 (ACM's default chain, RSA 2048, valid to 2038),
   optionally the other Amazon roots and Starfield Services Root G2 in case
   ACM changes the chain. Assign an owner and update procedure for CA
   rotation. **This is the expected choice.**
3. **SPKI pinning** only if CA validation cannot fit and the endpoint operator
   can provide an explicit key-rotation mechanism.

Do not pin a short-lived leaf certificate and do not install a general-purpose
browser CA bundle on this MCU.

The CA certificate is public data and should be stored as a const DER array in
a User-owned source file. Wi-Fi passwords and future client private keys remain
outside version control. The first implementation is server-authenticated TLS;
mTLS is out of scope.

### Certificate time

The RTC is enabled, clocked from the LSI and trimmed per board. It is set
from the `time` record of each successful astro API response; see
[RTC.md](../RTC.md). The generated SNTP client (`LWIP/App/sntp.c`) is not
started and must stay off, as it would also write the RTC. This changes the
certificate-time problem but does not solve it:

- **Bootstrap loop.** Over HTTPS the `time` record arrives only after the
  handshake. After a power loss the RTC is unset (the backup domain is lost;
  there is no LSE or battery), so the first handshake has no trusted time to
  check the certificate's validity period against. A reset or re-flash keeps
  the time.
- **Local time, not UTC.** The RTC holds the server configuration's local time
  with daylight saving applied, and the firmware keeps no time zone. X.509
  validity is in UTC, so the check needs the offset, or a margin of at least
  the largest offset.
- **Accuracy.** Between syncs the LSI drifts by up to about 800 ppm, about
  70 s a day; this is negligible against certificate lifetimes.

Select and document one production policy, for example:

- with an unset RTC, skip only the validity-period check (never the chain or
  hostname) on the first connection, set the RTC from that authenticated
  response, and require full validation afterwards; or
- have the payload carry UTC (or the offset) as well as local time, so the RTC
  or a separate UTC value can be checked directly; or
- use another product-approved authenticated time source compatible with the
  chosen pinning policy.

Unauthenticated SNTP alone must not be treated as the root of trust for the
first TLS connection. Whatever is chosen, the time a certificate is checked
against must never come from an unauthenticated response.

For calibration: ST's own `ST67W6X_FOTA_LWIP` configuration does not define
`MBEDTLS_HAVE_TIME`, so it never checks validity dates and relies on chain and
hostname verification alone. That is a defensible policy for this product too,
provided it is written down; the dates only add protection against a leaked
but expired certificate.

### Entropy

STM32G0B1 has no hardware RNG at all; ST's example seeds mbedTLS
(`MBEDTLS_ENTROPY_HARDWARE_ALT`, `MBEDTLS_NO_PLATFORM_ENTROPY`) from the U5's
RNG peripheral in `MBEDTLS/Target/hardware_rng.c`, which this MCU cannot copy.
The T02 driver exposes no random API from the module. The entropy source must
be suitable for seeding a client DRBG and must fail closed if unavailable. The
candidates are mbedTLS's NV seed (`MBEDTLS_ENTROPY_NV_SEED`) kept in the
settings EEPROM and re-stirred on every boot, combined with a jitter source
such as sampling the LSI-clocked RTC against the HSI-clocked timer, with a
written analysis of the entropy per sample. Do not substitute tick, MAC
address, ADC noise without analysis, or a fixed seed. This is the largest open
item of Route A and does not exist in Route B.

### Protocol profile

Start with TLS 1.2 and the smallest cipher/signature set supported by the
production endpoint: ECDHE with P-256, RSA-2048 signatures (ACM default; add
ECDSA P-256 only if the ACM certificate is reissued as ECDSA), AES-128-GCM and
SHA-256. Add TLS 1.3 only if required and after
measuring its flash/RAM cost. Require SNI and hostname verification using
the resolved API host (`ApiTarget::host`: the saved `api host`, or
`APP_ST67_HTTP_HOST` as the fallback); ALPN should advertise only `http/1.1`.

## 4. Phase 1: Module Image and CubeMX (Route B) — done

1. **Module image.** Program `st67w611m_mission_t01_v2.0.106.bin` through
   `firmware/Bypass` with the project's flash configuration, which writes a
   LittleFS holding only the trust anchor:

   ```bash
   cd firmware/Bypass
   ./tools/Build-LittleFS.sh
   ./tools/Query-ST67.sh --port COMx          # non-destructive identity check
   ./tools/Program-ST67.sh --port COMx --profile MissionT01 \
       --config-path tools/astroweather_t01_flash_prog_cfg.ini --force
   ```

   The vendor LittleFS (31 sample files) must not be used: the driver lists
   the module's file system before every certificate upload and times out on
   it ([What the T01 driver does](#what-the-t01-driver-does)). The
   certificate in `Bypass/tools/littlefs/Certificates/lfs/` must stay
   byte-identical to `User/Src/WiFi/TrustedCa.cpp`.
2. **CubeMX.** X-CUBE-ST67W61 1.3.0, *ST67 Architecture* T01, LwIP unticked,
   regenerated; `ST67_ARCH=W6X_ARCH_T01`. `LWIP/` and the LwIP middleware
   sources are removed. `w61_driver_config.h` keeps its `USER CODE EC`
   overrides (task priorities 46/47, SPI stack 1536); `W61_AT_LOG_ENABLE`
   stays 0 in the repository.
3. **Size.** Debug `.bss` 138 412 → 90 264 B with the switch, 83 808 B after
   the heap reduction in phase 5; Release text 110 152 B.

Host and module change together: `W6X_Init()` refuses a module running the
other architecture, so a T02 host stops at `w6x-init` (`DriverFailure`)
against a T01 module and vice versa. Rolling back means
`Program-ST67.sh --profile MissionT02` and a host from before this work.

## 5. Phase 2: Network Session and HTTPS Client — done

**Session** (`St67NetworkSession`): `W6X_Init()`, `W6X_RegisterAppCb()` with
Wi-Fi, **net** and error callbacks, `W6X_WiFi_Init()`, `W6X_Net_Init()`. The
net callback is mandatory: without it `W6X_Net_Init()` fails and its own error
path asserts in `vQueueDelete`. After `W6X_WiFi_Connect()` the station may
already report `GOT_IP`; both `CONNECTED` and `GOT_IP` are accepted, then
`waitForDhcp()` waits for `GOT_IP` (event, plus a 100 ms state poll) up to
`APP_ST67_DHCP_TIMEOUT_MS`. `stop()` deinitialises Net, WiFi and W6X in that
order.

**DNS** (`St67HttpFetcher`): `W6X_Net_ResolveHostAddress()` in the module.
The host name from `resolveApiTarget()` is kept for `Host`, SNI and the
module's name check; the address is never used for verification.

**HTTP client** (`HttpClient::get()`): the driver's `W6X_HTTP_Client_Request()`
is not used (it ignores `timeout` and `max_response_len`, accepts only 200,
does not check `Content-Type` and runs a task per request). Instead the
existing bounded parser (`HttpResponseParser`) runs over a `W6X_Net` socket:

1. `W6X_Net_Socket(AF_INET, SOCK_STREAM, IPPROTO_TLS_1_2)`, or `IPPROTO_TCP`
   when built with `APP_ST67_HTTP_USE_TLS=0`; `SO_RCVTIMEO`/`SO_SNDTIMEO` of
   `APP_ST67_HTTP_IO_TIMEOUT_MS`.
2. `W6X_Net_TLS_Credential_AddByContent()` with the anchor, under the
   socket's number as tag; `TLS_SEC_TAG_LIST`, `TLS_HOSTNAME` (SNI) and
   `TLS_ALPN_LIST` `http/1.1`. With a CA set the driver sends
   `AT+CIPSSLCCONF` auth mode 2, server authentication.
3. `W6X_Net_Connect()` does TCP and the handshake. The request-wide deadline
   `APP_ST67_HTTP_TOTAL_TIMEOUT_MS` (15 s) starts here and is checked before
   every read.
4. One `GET` with `Connection: close`; headers into a 2 KiB buffer, body
   handed to the fetcher's callback with the 4096-byte limit and CRC.
5. Socket closed and the credential tag released on every path; the result
   callback runs exactly once.

The fetch task stack is 4096 B (2560 B overflowed with the driver's AT trace
on).

## 6. Phase 3: Trust Material, Configuration and Failure Reporting — done

- **Trust anchor:** Amazon Root CA 1 as PEM in `TrustedCa.cpp`
  (`kAnchorName`, `kAnchorPem`); the chain observed on CloudFront is in
  [Two routes](#2-two-routes). The module keeps the file in its LittleFS; the
  driver compares it with the host's copy before each request and rewrites it
  when it differs (once per boot in practice, see the bench record).
  Rotation: add the new root to `TrustedCa.cpp` and to
  `Bypass/tools/littlefs/`; the host build is what matters, since the driver
  uploads a changed file by itself, and the module LittleFS only needs
  rebuilding to keep its listing short.
- **Bench anchor:** `cmake -DAPP_ST67_TLS_BENCH_ANCHOR_ISRG=ON` trusts ISRG
  Root X1 for the badssl.com cases. Off by default; the fetch log line ends
  in `ca=<anchor>` so a bench image is recognisable. No build has a
  verification-off switch.
- **Configuration** (`app_config.h`): `APP_ST67_HTTP_USE_TLS` (1),
  `APP_ST67_HTTPS_PORT` (443), `APP_ST67_HTTP_PORT` (80, plain bench build
  only), `APP_ST67_HTTP_IO_TIMEOUT_MS` (5 s), `APP_ST67_HTTP_TOTAL_TIMEOUT_MS`
  (15 s). `APP_ST67_DNS_TIMEOUT_MS` is no longer used.
- **Failures:** every HTTP or TLS failure fails the `fetch` stage, reported
  to clients as `HttpFailure`; `net-init` joins `w6x-init` and `wifi-init` as
  `DriverFailure`. The log line
  `ST67 https failed: <step> status=<http> bytes=<n> elapsed=<ms> w6x=<status> in <driver function>`
  names the step: `socket`, `tls-setup` (credential upload, SNI, ALPN),
  `connect` (TCP or a refused handshake), `send`, `response`, `timeout`.
  The module gives no reason for a refused handshake, so a wrong CA, a wrong
  name and an unreachable server all read as `connect`.
- **Time:** the policy as built relies on chain and hostname verification,
  which the module enforces. Whether it also checks validity dates is not
  established (no SNTP is configured; see the bench record). The host RTC is
  still set from the API's `time` record and plays no part in TLS.
- **Logging:** no certificate contents, payloads or credentials are logged.
  The driver's AT trace (`W61_AT_LOG_ENABLE`) does log the Wi-Fi password and
  is for the bench only ([WiFi.md](../WiFi.md#driver-at-trace)).

## 7. Phase 4: Automated Tests — partly done

Native tests (`ctest`, 15 suites) cover the transport-independent parts:
`HttpResponseParser` (header split points, limits, `Content-Length` cases),
the host/path and `Content-Type` rules, the connect diagnosis, and the
stage-to-status mapping including `net-init`.

Not covered natively, and left to the bench: `HttpClient::get()` itself (its
socket calls are the driver's), the total deadline, and every TLS policy
question, which the module answers. Open: a fake `W6X_Net` socket layer to run
`HttpClient::get()` against fragmented reads, timeouts and early closes, which
would also pin the deadline and the exactly-once result callback.

## 8. Phase 5: Bench Validation — done except where marked

Run on board 1 on 2026-10-04; results in the
[bench record](#bench-record-2026-10-04-route-b) below, procedures in
[Testing.md](../Testing.md#bench-tests) and the `wifi stress` command
([Console.md](../Console.md#wifi)). `tools/console_capture.ps1` holds one
console session per board reset, which the development PC requires.

| Step | Status |
| --- | --- |
| Debug and Release build, native tests, `git diff --check` | done |
| Size against the plain-HTTP baseline | done (phase 1) |
| One HTTPS fetch: DNS, TLS, status, type, length, CRC, disconnect, recovery | done |
| Handshake and request time, `heapFree`/`heapMin`, task count | done; stack high-water of `St67HttpFetch` at 4096 B **not yet read** (`stats on`) |
| Certificate failure cases fail closed, next request succeeds | done for wrong CA, hostname mismatch, untrusted root, self-signed, other root; **expired certificate outstanding** |
| Delayed, truncated, oversized and reset responses within their deadlines | **not run**; one natural `connect` failure recovered in the stress run |
| 100 persistent HTTPS cycles | done twice (100/100 at 40 000 B heap, 99/100 at 32 000 B) |
| Alternating success/failure batch | **not run** as a batch; failure cases were interleaved with successes by hand |
| Concurrent USB log traffic | **not run** |

Heap floor: 8 KiB minimum-ever free. Measured `heapMin` 16 072 to 16 368 B
with the heap at 32 000 B, so the margin is about 8 KiB above the floor.

### Bench record, 2026-10-04 (Route B)

Board 1, module `mission_t01_v2.0.106` with the slim LittleFS, Debug host
builds of this branch, access point `lemo`, all from the USB console. The
failure cases ran on a build configured with
`-DAPP_ST67_TLS_BENCH_ANCHOR_ISRG=ON`, which trusts ISRG Root X1 instead of
Amazon Root CA 1, with the server chosen by `api host`; `api default` restored
the saved host afterwards. The firmware's failure line names the step:
`connect` is a refused TLS handshake, `response` means the handshake
completed and the HTTP exchange followed.

| Case | Server | Chain under the anchor | Name | Result |
| --- | --- | --- | --- | --- |
| Production fetch, production anchor | `api.int.astroweather.albedoonline.com` | Amazon Root CA 1, trusted | match | **HTTP 200**, 1904..1906 B, CRC valid, parse OK; 13 s request to result, handshake about 1 s; `heapMin` 24 368 B after two fetches (run three times across resets) |
| Wrong CA | same server, ISRG anchor | untrusted | match | **refused in `connect`** |
| Positive control, ISRG anchor | `sha256.badssl.com`, `rsa2048.badssl.com` | trusted | match | handshake and HTTP completed, **status 200** (fetch then fails on the `text/plain` Content-Type rule, as designed) |
| Hostname mismatch | `wrong.host.badssl.com` (serves the same `*.badssl.com` certificate as `badssl.com`) | trusted | **mismatch** | **refused in `connect`**, while `badssl.com` with the same certificate passed the handshake: the module checks the name against the SNI |
| Untrusted root | `untrusted-root.badssl.com` | untrusted | match | refused in `connect` |
| Self-signed | `self-signed.badssl.com` | untrusted | match | refused in `connect` |
| Other root, ECDSA chain | `ecc256.badssl.com` (ISRG Root X2) | untrusted | match | refused in `connect` |
| Expired certificate | not tested | | | `expired.badssl.com` chains to COMODO, so it would fail on the chain, not the date; needs a server with an expired leaf under a trusted root. Since the production fetch passed with no SNTP configured, the module either checks dates against a clock it set itself or does not check them. Left open. |
| 100 persistent cycles (`wifi stress`, mode 3) | production API, production anchor | trusted | match | **100/100 complete in 21.5 min**, about 12.9 s per cycle including the 1 s gap. Free heap 29 384 B after every cycle, `heapMin` 24 368 B from cycle 1 to 100, 13 tasks throughout. After the batch's `stop()`: 38 840 B free (39 080 at boot), 11 tasks. A `wifi test` 8 min later re-initialised the driver from the powered-down module and fetched normally (`heapMin` 24 112 B), the restart path previously untested |
| 100 persistent cycles, heap reduced to 32 000 B | production API, production anchor | trusted | match | **99/100**: cycle 41 failed in `connect` after 8.9 s with no driver error (TCP or handshake to CloudFront not completed in time), cycle 42 onward recovered with no reset. Free heap 21 384 B after cycles 1 to 40, 21 352 B after the failed cycle and every cycle after it: **the failed connect retained 32 B**, success cycles retain nothing. `heapMin` 16 368 then 16 336 B. After `stop()` 30 808 B free (31 080 at boot, about 270 B retained per stop); `wifi test` afterwards passed, `heapMin` 16 072 B |

Conclusions: chain verification and hostname verification are enforced by
the module and fail closed with no response bytes delivered; the trust
model in section 3 (Amazon Root CA 1 only) works against CloudFront; TLS 1.2
is negotiated (OpenSSL sees 1.3 from the same server); 200 cycles over two
runs show no heap, task or socket trend on success, and the module restarts
from power-down. Two small retentions to keep an eye on, neither a threat
to the 16 KiB margin: a failed `connect` keeps 32 B (one in 200 cycles), and
each `stop()`/re-init keeps about 240 to 270 B; a daily fetch never calls
`stop()`. Open: the date check, and one observation: `badssl.com` itself (11 673-byte
body) completed the handshake but delivered no parsable status line
(`response`, status 505) where the 500-byte hosts delivered `200`; bodies
over 4 KiB are outside this product's envelope, but the behaviour of the
module's socket with a response larger than its receive buffer is worth a
trace when convenient.

Driver quirk seen on every boot: after the module restarts it reports the
stored certificate's size rounded up to 256 bytes (1208 as 1280), so the
driver's size comparison fails and it rewrites the file once per boot, about
3 s; within a boot the size matches and the content is compared instead.

## 9. File Changes (Route B, as built)

| File or area | Change |
| --- | --- |
| `HostControllerA.ioc`, generated CMake | ST67 Architecture T01, LwIP removed; FreeRTOS heap 32 000 B |
| `LWIP/`, `Middlewares/Third_Party/LwIP`, `SICS_Network_LwIP` | removed |
| `Appli/App/app_config.h` | `APP_ST67_HTTP_USE_TLS`, `APP_ST67_HTTPS_PORT`, `APP_ST67_TLS_BENCH_ANCHOR_ISRG` |
| `CMakeLists.txt` | `APP_ST67_TLS_BENCH_ANCHOR_ISRG` option |
| `User/Inc/WiFi/HttpClient.hpp`, `User/Src/WiFi/HttpClient.cpp` | `HttpClient::get()` over a `W6X_Net` socket, own request/result types and result codes |
| `User/Inc/WiFi/TrustedCa.hpp`, `User/Src/WiFi/TrustedCa.cpp` | trust anchor PEM, bench anchor |
| `User/Src/WiFi/St67HttpFetcher.cpp`, `User/Inc/WiFi/St67Runtime.hpp` | module DNS, TLS request, failure log line |
| `User/Src/WiFi/St67NetworkSession.cpp`, `St67NetworkAdapter.*` | `W6X_Net_Init`, net callback, `GOT_IP`, station state only |
| `User/Src/WiFi/St67HttpFetchTask.cpp` | stack 4096 B |
| `User/Src/WiFi/St67FetchStatusMap.cpp`, `tests/` | `net-init` stage |
| `User/Src/Console/*` | `wifi stress`; https in `status`, `api` and help |
| `firmware/Bypass/tools/` | `Build-LittleFS.sh`, `astroweather_t01_flash_prog_cfg.ini`, the anchor PEM |
| `tools/console_capture.ps1` | bench console capture |
| `docs/` | WiFi, Architecture, RAM, CubeMX compliance, RTC, Testing, Development, Console |

## 10. Validation Commands

After a CubeMX regeneration and after each change:

```bash
cmake --preset Debug && cmake --build --preset Debug
cmake --preset Release && cmake --build --preset Release
cmake --preset NativeTests && cmake --build --preset NativeTests
ctest --test-dir build/native-tests-local --output-on-failure
arm-none-eabi-size -A -d build/Debug/HostControllerA.elf
git diff --check
```

Use the bundled Cube CMake in place of `cmake` where it is not on `PATH`; see
[Development.md](../../../Docs/Development.md). A Debug tree configured for the bench
anchor stays so until reconfigured with `-DAPP_ST67_TLS_BENCH_ANCHOR_ISRG=OFF`.
On the bench: `wifi test` for one fetch, `wifi stress` for 100 cycles, and the
certificate cases in [Testing.md](../Testing.md#bench-tests). Bench results
should identify firmware build, module image, endpoint, cycle count, first
failure, timings, bytes/CRC and heap minima, without secrets or payloads.

## 11. Exit Criteria

| Criterion | State |
| --- | --- |
| TLS configuration owned by CubeMX and the module, surviving regeneration | met: T01 selected in the `.ioc`, no generated file edited outside USER sections |
| Production endpoint succeeds with required chain and hostname verification | met |
| Time and entropy policies implemented and documented | entropy: the module's; time: chain and hostname only, date check **open** |
| Plain HTTP and HTTPS share the same bounded, tested parser | met |
| Connect, handshake, send, receive, total request and cleanup bounded | met (driver timeouts plus the 15 s total deadline) |
| Certificate and transport failures fail closed and the next request recovers without reset | certificate cases met except expiry; transport fault cases **not run** |
| 100 persistent cycles and failure cycles show no resource trend | persistent: met twice; a failed connect retains 32 B and a `stop()` about 250 B, recorded; alternating batch **not run** |
| Worst-case flash, static RAM, heap and stack keep approved margins | flash, RAM and heap met; fetch-task stack high-water **not read** |
| Debug and Release HostController plus Debug DisplayController build | met |
| No secret, private key, payload or insecure verification mode committed or logged | met; the AT trace that logs the Wi-Fi password is off in the repository |

HTTPS is in use on `main` since 2026-10-04 with the items marked open above
outstanding. Do not ship a build with certificate verification disabled, on
either route.

## Appendix A. Route A Phases (mbedTLS on the Host, T02), Not Taken

Kept as the fallback should the module's TLS ever prove inadequate, for
example after a CloudFront policy change it cannot negotiate. Written before
Route B was tried; the section 3 decisions on certificate time and entropy
apply here, and the board would first have to return to T02 (module image
and CubeMX).

### A.1 Phase 1: Enable mbedTLS Through CubeMX

This phase is a configuration handoff because middleware selection is
CubeMX-owned.

1. In STM32CubeMX, under Software Packs > X-CUBE-ST67W61, tick
   **Security > mbedTLS** (3.6.4); it is only selectable with LwIP and T02.
2. Select only required client features: TLS client, X.509 parsing and
   verification, PEM only if certificates are not compiled as DER, SHA-256,
   the endpoint's key/signature algorithms, SNI, and optional maximum fragment
   length support.
3. Disable server mode, DTLS, legacy protocol versions, unused ciphers,
   filesystem support, and debug tracing in production.
4. Regenerate the project.
5. Verify that generated CMake contains mbedTLS sources, include paths,
   libraries, and `MBEDTLS_CONFIG_FILE`, and that generated configuration
   changes are confined to expected files and USER sections.
6. Build both firmware variants before application changes. This isolates
   middleware-integration failures from HTTPS-client failures.
7. Record flash, `.data`, and `.bss` deltas from the plain-HTTP baseline
   (2026-10-02: Debug text 391 496 B, Release text 197 640 B, `.bss` 138 412 B
   in Debug). Expect the `-O0` Debug build to need mbedTLS compiled at `-Os`.
8. Set `MBEDTLS_SSL_IN_CONTENT_LEN`/`MBEDTLS_SSL_OUT_CONTENT_LEN` below the
   16 KiB defaults from the start; the server's certificate message is about
   4 KiB and CloudFront is not guaranteed to honour the maximum-fragment-length
   extension, so the inbound size must be found by test, not set to the
   response limit.

If the generated middleware cannot fit the image or conflicts with the package
version, stop and resolve that constraint rather than manually copying an
untracked mbedTLS build into generated CMake.

### A.2 Phase 2: Add a User-Owned TLS Transport

Keep HTTP parsing independent from the byte transport.

1. Refactor `User/Src/WiFi/HttpClient.cpp` behind a small internal transport
   contract with `connect`, `writeAll`, `read`, `close`, and error reporting.
   Preserve the public fetch/result behavior during this step.
2. Keep the current LwIP socket implementation as `PlainTcpTransport`.
3. Add a User-owned `TlsTransport` under `User/Inc/WiFi` and
   `User/Src/WiFi` using the CubeMX-provided mbedTLS APIs.
4. Give each request its own `mbedtls_ssl_context`. A shared immutable or
   serialized TLS configuration/CA chain may be considered only after ownership
   and cleanup are proven. Do not use file-static per-connection pointers.
5. Bind mbedTLS BIO callbacks to the request's LwIP socket. Translate socket
   timeout, retry, peer-close, and reset conditions into distinct transport
   results.
6. Configure client mode, SNI, expected hostname, CA chain, ALPN `http/1.1`,
   required certificate verification, and the approved time/entropy sources.
7. Drive `mbedtls_ssl_handshake`, `mbedtls_ssl_write`,
   `mbedtls_ssl_read`, and `mbedtls_ssl_close_notify` with bounded loops. Every
   loop must check both the operation timeout and one request-wide deadline.
8. Centralize cleanup so partially initialized certificates, DRBG/entropy,
   config, SSL context, and socket are released exactly once in reverse order.
9. Preserve the existing bounded HTTP parser above both transports. A TLS
   record boundary must have no effect on HTTP header/body parsing.

Prefer static or reusable allocation where mbedTLS permits it. If dynamic
allocation remains necessary, record peak block sizes and prove that alternating
success/failure requests do not fragment the FreeRTOS heap.

### A.3 Phase 3: Endpoint and Result Integration

1. Add an explicit transport selection to non-secret configuration; do not
   infer security solely from port number. Keep port independently configurable
   and default HTTPS to 443.
2. Extend the `HttpClient_Get` request contract to receive transport, trust
   material, and the total deadline without exposing mbedTLS types to
   `St67HttpFetcher`.
3. Continue resolving the API host (`resolveApiTarget()`) through LwIP DNS and pass the original
   hostname separately for the HTTP `Host` header, SNI, and certificate hostname
   verification. Never verify against the resolved IP address.
4. Add TLS-specific fetch results or detail codes for configuration, entropy,
   time, handshake, untrusted CA, hostname mismatch, certificate validity,
   record I/O, and close failures. Preserve the first authoritative failure.
5. Log only stage, mbedTLS error code/string, elapsed time, and verification
   flags. Do not log credentials, certificate contents, private keys, or response
   payloads.
6. Keep one active request at a time through `St67HttpFetchTask`. A failed HTTPS
   request must still disconnect cleanly and permit the next request without a
   host reset.
7. Update `app_credentials.h.template` with empty endpoint values only. Put the
   selected CA in a separate non-secret source/header, not in the credentials
   file.

### A.4 Phase 4: Automated Tests

Refactor only enough pure logic from `HttpClient.cpp` to make it host-testable,
then add focused native tests.

#### HTTP-over-transport tests

Run the same parser suite against fragmented fake transport reads:

- header split at every byte position, including `\r\n\r\n` across reads;
- body bytes arriving in the same read as the final header bytes;
- exact-limit and oversized headers and bodies;
- valid, duplicate/conflicting, malformed, and truncated `Content-Length`;
- close-delimited response;
- short writes and repeated reads;
- timeout/error at connect, write, header, body, and close;
- exactly one completion notification and cleanup on every path.

#### TLS policy tests

Where the selected mbedTLS build supports host tests, use deterministic local
certificates and a local TLS server to cover:

- valid chain and matching hostname;
- unknown CA;
- hostname mismatch;
- expired and not-yet-valid certificates;
- missing or invalid trusted time;
- truncated handshake and peer reset;
- TLS records split independently of HTTP boundaries;
- deadline expiry during handshake and body transfer.

No test-only insecure verification switch may be reachable in production
configuration.

### A.5 Phase 5: Firmware and Bench Validation

Use a controlled HTTPS endpoint whose CA, hostname, response size, content type,
and failure modes are known.

1. Build `Debug` and `Release`; run native tests and `git diff --check`.
2. Compare ELF/map flash, `.data`, and `.bss` with the recorded plain-HTTP
   baseline.
3. Run one successful HTTPS GET and confirm DNS, TCP, SNI, chain validation,
   hostname validation, HTTP status/type, body length, CRC, disconnect, and
   recovery.
4. Record handshake time, total request time, `heapFree`, `heapMin`, task count,
   pbuf/socket state, and stack high-water marks. Exercise the deepest TLS error
   path before reducing any stack.
5. Run each certificate failure case and verify fail-closed behavior followed
   by a successful request.
6. Run delayed handshake/header/body, truncated response, oversized response,
   and connection-reset cases. All waits must finish within their documented
   deadlines.
7. Run 100 persistent HTTPS cycles with stable heap, tasks, pbufs, sockets, and
   stack margins.
8. Run an alternating success/failure batch and repeated maximum-size responses
   to expose cleanup errors and allocator fragmentation.
9. Repeat with concurrent USB debug traffic and verify that logging pressure
   does not alter request correctness.

Keep at least 8 KiB minimum-ever free FreeRTOS heap as the existing floor, and
set a larger TLS-specific margin before release based on measured worst-case
error paths. If TLS cannot maintain a defensible margin, reduce measured static
consumers first; do not weaken verification or silently increase buffers.
