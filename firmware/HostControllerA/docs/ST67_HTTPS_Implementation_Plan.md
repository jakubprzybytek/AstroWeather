# ST67 HTTPS Implementation Plan

**Status: not started as of 2026-09-23.** The firmware fetches over plain HTTP
only; the current Wi-Fi stack is described in [WiFi.md](WiFi.md).

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

## 2. Current State and Constraints

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
  credentials are stored in the EEPROM with `wifi set`; see [Settings.md](Settings.md).

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

Memory is a primary constraint. The STM32G0B1 has 144 KiB RAM, the current
image statically reserves about 90%, and the 40 KiB FreeRTOS heap is shared by
ST67 tasks, LwIP, application tasks, and future TLS allocations. TLS sizing
must be measured, not inferred from a successful link.

## 3. Decisions Required Before Implementation

Record these decisions in this document before enabling the production path.

### Trust model

Choose one:

1. **Pinned private CA** for an endpoint under project control. This is the
   preferred smallest trust store and simplifies certificate rotation if the
   issuing CA remains stable.
2. **Curated public roots** containing only roots needed by the production
   endpoint. Assign an owner and update procedure for CA rotation.
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
[RTC.md](RTC.md). The generated SNTP client (`LWIP/App/sntp.c`) is not
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

### Entropy

STM32G0B1 has no enabled hardware RNG in this project. Verify the entropy
source used by the selected mbedTLS package. It must be suitable for seeding a
client DRBG and must fail closed if unavailable. Investigate a documented
public ST67 random API or another board entropy source; do not substitute tick,
MAC address, ADC noise without analysis, or a fixed seed.

### Protocol profile

Start with TLS 1.2 and the smallest cipher/signature set supported by the
production endpoint and mbedTLS package. Add TLS 1.3 only if required and after
measuring its flash/RAM cost. Require SNI and hostname verification using
the resolved API host (`ApiTarget::host`: the saved `api host`, or
`APP_ST67_HTTP_HOST` as the fallback); ALPN should advertise only `http/1.1`.

## 4. Phase 1: Enable mbedTLS Through CubeMX

This phase is a configuration handoff because middleware selection is
CubeMX-owned.

1. In STM32CubeMX, enable the package-compatible mbedTLS middleware for the
   HostController image and its LwIP socket integration.
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
7. Record flash, `.data`, and `.bss` deltas from the plain-HTTP baseline.

If the generated middleware cannot fit the image or conflicts with the package
version, stop and resolve that constraint rather than manually copying an
untracked mbedTLS build into generated CMake.

## 5. Phase 2: Add a User-Owned TLS Transport

Keep HTTP parsing independent from the byte transport.

1. Refactor `User/Src/WiFi/HttpClient.cpp` behind a small internal transport
   contract with `connect`, `writeAll`, `read`, `close`, and error reporting.
   Preserve the public fetch/result behavior during this step.
2. Keep the current LwIP socket implementation as `PlainTcpTransport`.
3. Add a User-owned `TlsTransport` under `User/Inc/HostController` and
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

## 6. Phase 3: Endpoint and Result Integration

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

## 7. Phase 4: Automated Tests

Refactor only enough pure logic from `HttpClient.cpp` to make it host-testable,
then add focused native tests.

### HTTP-over-transport tests

Run the same parser suite against fragmented fake transport reads:

- header split at every byte position, including `\r\n\r\n` across reads;
- body bytes arriving in the same read as the final header bytes;
- exact-limit and oversized headers and bodies;
- valid, duplicate/conflicting, malformed, and truncated `Content-Length`;
- close-delimited response;
- short writes and repeated reads;
- timeout/error at connect, write, header, body, and close;
- exactly one completion notification and cleanup on every path.

### TLS policy tests

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

## 8. Phase 5: Firmware and Bench Validation

Use a controlled HTTPS endpoint whose CA, hostname, response size, content type,
and failure modes are known.

1. Build `Debug-HostController`, `Release-HostController`, and
   `Debug-DisplayController`; run native tests and `git diff --check`.
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

## 9. Expected File Changes

| File or area | Planned change |
| --- | --- |
| `HostControllerA.ioc` | User performs mbedTLS configuration in CubeMX (the RTC is already enabled) |
| Generated CMake/middleware configuration | Regenerated mbedTLS sources, includes, and config; no manual edits outside USER sections |
| `Appli/App/app_config.h` | Transport selection, HTTPS port, handshake/total deadlines, and TLS limits |
| `Appli/App/app_credentials.h.template` | Empty host/path values only |
| `User/Inc/HostController/HttpClient.hpp` | Transport-neutral request options and result contract |
| `User/Src/WiFi/HttpClient.cpp` | Shared bounded HTTP request/response logic over a transport |
| `User/Inc/HostController/TlsTransport.hpp` | User-owned TLS transport boundary |
| `User/Src/WiFi/TlsTransport.cpp` | Per-request mbedTLS setup, verified handshake, I/O, deadlines, cleanup |
| `User/Inc/HostController/TrustedCa.hpp` and corresponding source | Const DER trust anchor(s) and documented rotation metadata |
| `User/Src/WiFi/St67HttpFetcher.cpp` | Select transport, pass hostname/trust policy, map TLS failures |
| `tests/` | Transport-independent HTTP parser tests and TLS policy tests where feasible |
| `docs/Firmware-RAM-Usage.md` | Post-mbedTLS link and runtime measurements |

Names may be adjusted to existing conventions during implementation, but TLS
must remain User-owned and generated files must remain regenerable.

## 10. Validation Commands

After CubeMX regeneration and after each implementation increment:

```bash
cmake --preset Debug-HostController && cmake --build --preset Debug-HostController
cmake --preset Release-HostController && cmake --build --preset Release-HostController
cmake --preset Debug-DisplayController && cmake --build --preset Debug-DisplayController
cmake --preset NativeTests && cmake --build --preset NativeTests
ctest --test-dir build/native-tests-local --output-on-failure
arm-none-eabi-size -A -d build/Debug-HostController/HostControllerA.elf
git diff --check
```

Use the bundled Cube CMake in place of `cmake` where it is not on `PATH`; see
[Development.md](Development.md). The native-test build directory is set by the
`NativeTests` preset in `CMakePresets.json`.
Bench results should identify firmware build, endpoint certificate generation,
cycle count, first failure, timings, bytes/CRC, memory minima, task/stack counts,
and debug transport counters without recording secrets or payload data.

## 11. Exit Criteria

HTTPS support is complete only when:

- mbedTLS is enabled through the CubeMX-owned configuration and survives
  regeneration;
- the production endpoint succeeds with required CA-chain and hostname
  verification;
- trusted time and cryptographic entropy policies are implemented and
  documented;
- plain HTTP and HTTPS share the same bounded, tested response parser;
- connect, handshake, send, receive, total request, and cleanup are bounded;
- all certificate and transport failure cases fail closed and the next request
  recovers without reset;
- 100 persistent HTTPS cycles and alternating failure cycles show no resource
  trend or callback-after-free;
- worst-case flash, static RAM, heap, and stack measurements retain approved
  margins;
- Debug and Release HostController plus Debug DisplayController builds pass;
- no secret, private key, payload, or insecure verification mode is committed
  or logged.

If these gates cannot be met on STM32G0B1, document the measured limiting
resource and reconsider the endpoint trust model, protocol, or MCU. Do not ship
HTTPS with optional/disabled certificate validation.
