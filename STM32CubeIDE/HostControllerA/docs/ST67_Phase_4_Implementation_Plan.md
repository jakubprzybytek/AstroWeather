# ST67W611M1 Phase 4 Host HTTP Fetch Plan

## 1. Objective

Extend the proven Phase 3 station lifecycle with one bounded host-side fetch:

```text
trigger
  -> initialize W6X, Wi-Fi, and host LwIP
  -> connect and obtain DHCP
  -> resolve a configured hostname through host LwIP DNS
  -> perform one plain HTTP GET
  -> stream a bounded response into an application-owned consumer
  -> verify HTTP task and socket completion
  -> disconnect Wi-Fi
  -> apply the selected Phase 3 teardown policy
  -> report an authoritative result and resource measurements
```

The first acceptance target is a deterministic HTTP endpoint on the local
network. This isolates the host DNS, TCP, HTTP framing, and response-consumer
path from Internet reachability and TLS resource costs.

HTTPS is a separate Phase 4 track. It must not be enabled until plain HTTP is
stable and the mbedTLS trust, time, entropy, flash, heap, and stack decisions
in Section 12 are resolved.

## 2. Current status and constraints

Phase 3 has proven 100 persistent connect/DHCP/disconnect cycles with stable
heap and task count. The repeated full-shutdown path remains functionally
successful but loses heap across cold restart cycles. Phase 4 development and
stress testing must therefore use persistent initialization with automatic
standby first. A single full-shutdown fetch may be tested for functional
coverage, but it must not be used to claim Phase 4 resource stability.

The T02 architecture requires host LwIP for DNS, sockets, HTTP, and future
TLS. A successful Wi-Fi association is not sufficient: fetch may start only
after the station netif is link-up and DHCP has assigned a nonzero IPv4
address and usable DNS server.

`St67ServiceTask` remains the sole lifecycle owner. It decides when fetch may
start, waits for completion, and prevents disconnect or teardown while an
HTTP socket or worker is active. HTTP and DNS callbacks may copy bounded data
and signal the owner; they must not run lifecycle operations.

Credentials, private URLs, query secrets, authorization headers,
certificates, and response payloads must not be logged or committed. The
initial endpoint configuration should contain only a non-secret host, port,
path, expected content type, and response-size limit.

### 2.1 Single-cycle integration result (2026-08-23)

The first integrated fetch completed successfully from a cold
`CHIP_EN`-low start using the configured `jsonplaceholder.typicode.com`
endpoint and `SingleFullShutdown` mode. This was an Internet endpoint rather
than the planned deterministic local test server, so it proves the complete
DNS/TCP/HTTP path but does not replace the local endpoint matrix.

Observed sequence:

- ST67 middleware `1.3.0`, SDK `2.0.106`, module ID `C6AFDBD111400004`.
- Wi-Fi association succeeded on channel 2 at RSSI `-33`; DHCP assigned
   `192.168.1.142` with gateway and DNS server `192.168.1.1`.
- DNS resolved the configured hostname to `188.114.96.11` in `3133 ms`.
- TCP connection and HTTP request succeeded; the response was HTTP `200`.
- The response contained `83` bytes and produced CRC32 `b701dc37`.
- The bounded sanitized response preview printed the complete JSON payload in
   two records. Non-printable bytes would be rendered as `.`.
- The HTTP worker completed before disconnect. The temporary peak observed
   free heap was `21432 B`; it returned to `23984 B` after the HTTP task ended
   and reached `33896 B` after full teardown.
- Final teardown removed both netifs, stopped the worker, released pbufs, and
   reached `CHIP_EN=0` and `RDY=0`.
- The batch result was `pass=1 fail=0`. Minimum-ever free heap was `15016 B`;
   the ST67 service stack retained `576 B` at its lowest observed watermark.
- RX overflow and truncation remained zero. Five pre-existing USB
   `busyDrop` events did not increase during the lifecycle.
- A second switch press during the active batch was rejected as intended.

This closes the single-cycle functional integration check for DNS, plain HTTP,
bounded preview consumption, completion-before-disconnect, and cleanup. It
does not close Phase 4: persistent fetch stress, deterministic local endpoint
fault coverage, late-callback recovery, and the HTTPS track remain open. The
full-shutdown heap trend identified in Phase 3 also remains unresolved.

## 3. Source-backed findings in the generated HTTP client

`LWIP/App/http_client.c` is a useful starting point, but it is not ready to be
treated as a bounded production API without a focused hardening increment.

The current implementation has these relevant behaviors:

- `HTTP_Client_Request()` creates and connects the socket synchronously before
  it creates the HTTP worker task. A slow TCP connect can therefore block
  `St67ServiceTask` without a request-level connect deadline.
- The API comment says `settings` is optional, but the implementation
  dereferences and copies it unconditionally.
- The request object, URI, optional server name, request buffer, 4097-byte
  receive buffer, and HTTP task are dynamically allocated.
- `HTTP_CLIENT_THREAD_STACK_SIZE` is 2048 bytes and a new task is created for
  every request. Stack and heap cost must be measured with the existing W6X
  and LwIP tasks active.
- `HTTP_connection_t::max_response_len` is now enforced for declared and
   streamed response bytes by the implementation.
- `HTTP_connection_t::timeout` is not used as the complete request timeout;
  the socket receive timeout is controlled by the compile-time
  `HTTP_CLIENT_TCP_SOCK_RECV_TIMEOUT` value.
- The result callback now runs after socket and request-object cleanup, and an
   idle query is available to the lifecycle owner.
- Oversize responses and header-consumer rejection now produce failures, but
   the generated client still needs targeted tests for every error path.
- Responses without `Content-Length` still use an incomplete trailing
  `CRLF CRLF` body test. It can inspect before the current buffer when fewer
  than four bytes are received and does not implement close-delimited or
  chunked HTTP body framing.
- Header parsing is bounded by a 4096-byte working buffer, but response framing
  support and status handling need explicit acceptance tests.
- Normal body callbacks use `recv_fn_arg`; the error path still uses
   `callback_arg`, so callback argument ownership needs a follow-up cleanup.
- A cooperative cancellation API now exists, but connect-time cancellation,
   a complete total-deadline implementation, and a formal join result still
   need targeted failure-path validation.
- TLS state is held in file-static pointers, so concurrent HTTPS requests are
  unsafe even after mbedTLS is added.

These are Phase 4 implementation requirements, not reasons to replace the
client wholesale. Prefer a small, testable hardening of this client and keep
one in-flight request at a time.

## 4. Phase boundary and non-goals

Phase 4 includes:

- one IPv4 hostname resolution per fetch;
- one non-proxy HTTP GET with `Connection: close`;
- HTTP/1.1 status and header validation;
- fixed-length and close-delimited response bodies;
- optional chunked decoding only if the selected production endpoint needs it;
- a bounded streaming consumer and exact byte accounting;
- deterministic timeout, oversize, parse, consumer, and transport failures;
- repeated fetch/connect/disconnect stress in persistent lifecycle mode;
- an HTTPS feasibility measurement and design decision after HTTP acceptance.

Phase 4 does not include:

- daily RTC scheduling or retry windows;
- domain-specific weather-data parsing;
- redirects, cookies, compression, proxy support, uploads, or persistent HTTP
  connections;
- concurrent requests;
- production credential or token provisioning;
- selecting full shutdown while the Phase 3 cold-restart heap trend remains
  unresolved.

Redirects should initially return a stable `http-status` failure. If the final
endpoint requires redirects, add a bounded redirect count, allow only
configured schemes and hosts, and repeat DNS validation for each destination.

## 5. Proposed application boundary

Keep network lifecycle control in `St67ServiceTask`, but isolate transaction
state and response consumption from Wi-Fi control. A small private fetch
component should expose one in-flight operation and an authoritative terminal
result.

Suggested result model:

```cpp
enum class FetchStage : uint8_t {
  None,
  Configuration,
  DnsStart,
  DnsWait,
  TcpConnect,
  RequestStart,
  Headers,
  Body,
  Complete,
  Cancel,
};

enum class FetchError : uint8_t {
  None,
  InvalidConfiguration,
  DnsFailure,
  Timeout,
  Transport,
  InvalidResponse,
  HttpStatus,
  UnsupportedFraming,
  UnexpectedContentType,
  ResponseTooLarge,
  ConsumerRejected,
  InternalResource,
};

struct FetchResult {
  FetchStage stage;
  FetchError error;
  int32_t detail;
  uint16_t httpStatus;
  uint32_t declaredBytes;
  uint32_t receivedBytes;
  uint32_t elapsedMs;
};
```

The exact type names may follow the existing local style, but preserve these
properties:

- the first failure remains authoritative through disconnect and cleanup;
- HTTP status is separate from LwIP/socket and parser errors;
- declared and received sizes are separately recorded;
- transaction completion means the receive loop ended, the socket closed, and
  the HTTP worker released its allocations;
- callback data is copied or consumed synchronously and never retained.

Use an application-owned bounded consumer interface. For the first increment,
the consumer may calculate total length and CRC32 while retaining at most a
small fixed preview for deterministic comparison. Do not allocate a buffer
from remote `Content-Length`. Domain parsing and ownership transfer belong to
a later increment after transport behavior is proven.

## 6. Configuration

Add non-secret defaults to `Appli/App/app_config.h`:

```c
#define APP_ST67_HTTP_PORT                 80U
#define APP_ST67_DNS_TIMEOUT_MS          5000U
#define APP_ST67_TCP_CONNECT_TIMEOUT_MS  5000U
#define APP_ST67_HTTP_IO_TIMEOUT_MS      5000U
#define APP_ST67_HTTP_TOTAL_TIMEOUT_MS  15000U
#define APP_ST67_HTTP_MAX_HEADER_BYTES   2048U
#define APP_ST67_HTTP_MAX_RESPONSE_BYTES 4096U
#define APP_ST67_HTTP_STRESS_CYCLES       100U
```

Provide endpoint values through a product configuration boundary with benign
defaults or an ignored local override:

```c
#define APP_ST67_HTTP_HOST ""
#define APP_ST67_HTTP_PATH ""
#define APP_ST67_HTTP_EXPECTED_CONTENT_TYPE "application/json"
```

Validation requirements:

- host is nonempty and no longer than the client's host limit;
- path is nonempty, begins with `/`, and contains no CR or LF;
- port is nonzero and is not 443 in the plain-HTTP mode;
- all timeout and size limits are nonzero and safely convertible to the LwIP
  socket and RTOS timeout types;
- stress count is nonzero;
- host/path are never printed if product policy treats them as sensitive.

Do not accept a complete URL in the first implementation. Separate host,
port, and path avoid ad hoc URL parsing and make DNS, `Host`, and request-target
ownership explicit.

## 7. Implementation sequence

### Increment 4.0 - Freeze the Phase 3 baseline

1. Keep the checked-in normal lifecycle behavior unchanged while the HTTP
   client is hardened.
2. Save one successful persistent connect/DHCP/disconnect run with heap, task,
   pbuf, DebugService stack, and ST67 service stack measurements.
3. Record the Debug transport counters before the run.
4. Build both Debug variants before introducing HTTP behavior.

Acceptance check: the existing persistent lifecycle still passes and provides
the comparison baseline for HTTP task, socket, and buffer costs.

### Increment 4.1 - Harden the HTTP client contract

Modify `http_client.h/.c` before calling it from the lifecycle:

1. Validate all required pointers and scalar inputs before socket creation.
   Either make `settings` mandatory in the header or safely supply defaults
   when it is null.
2. Enforce one in-flight request. Reject a second request deterministically.
3. Apply bounded connect, send, receive, and total transaction deadlines.
   A receive timeout alone is not a total deadline.
4. Enforce `max_response_len` before accepting a declared length and before
   delivering every body chunk. Detect integer overflow in byte accounting.
5. Separate the header/status notification from the final completion callback.
   Emit exactly one terminal completion after socket closure and allocation
   cleanup is committed.
6. Propagate consumer rejection and oversize as failures rather than normal
   success.
7. Make callback argument use consistent on success and error paths.
8. Replace the unknown-length body heuristic with correct close-delimited
   handling. Detect `Transfer-Encoding: chunked`; either decode it correctly
   with bounds or reject it as `unsupported-framing` for the first milestone.
9. Reject malformed/conflicting framing, including invalid `Content-Length`,
   duplicate unequal lengths, and a response that declares both unsupported
   transfer encoding and content length.
10. Add an observable idle/completed state so teardown can prove no HTTP task
    or socket remains active. Cancellation must close/shutdown the socket,
    cause the worker to exit, and have a bounded join wait.
11. Keep HTTPS disabled and avoid changing TLS code in this increment.

Prefer moving blocking socket creation/connect into the worker so the owner is
not blocked before it can enforce the total deadline. If a minimal wrapper
cannot provide reliable cancellation and join semantics, replace the per-call
detached task with a single persistent HTTP worker owned by the application.
Do not delete a task externally while it owns a socket or heap allocations.

Acceptance check: host-side review or targeted tests prove exactly one terminal
callback and complete cleanup for success, DNS-independent connect failure,
receive timeout, malformed header, unsupported framing, oversized declared
body, oversized streamed body, and consumer rejection.

### Increment 4.2 - Add bounded DNS resolution

1. Add a DNS helper using LwIP `dns_gethostbyname()` and its completion
   callback. Handle both immediate cache hits and asynchronous completion.
2. Copy the resolved address into task-owned storage and signal
   `St67ServiceTask`; do not retain LwIP callback pointers.
3. Clear stale DNS flags and results before each request.
4. Wait no longer than `APP_ST67_DNS_TIMEOUT_MS` and distinguish timeout from
   an explicit DNS error.
5. Accept IPv4 only for the initial milestone and reject unspecified,
   multicast, broadcast, or loopback results.
6. Do not disconnect while a DNS callback can still target destroyed request
   state. Use persistent callback storage or a generation identifier so a late
   callback is harmless.

Acceptance check: repeated cache miss and cache hit resolutions produce the
same address, an unknown host fails within the bound, and a late callback
cannot complete a newer request.

### Increment 4.3 - Integrate one HTTP fetch

Extend `runStationIteration()` only after DHCP succeeds:

```text
connect -> DHCP -> DNS -> HTTP GET -> HTTP idle -> disconnect
```

1. Add `Resolving` and `Fetching` lifecycle states or equivalent explicit
   stage tracking.
2. Validate endpoint configuration before W6X initialization when possible.
3. Resolve the configured host and log only stage, elapsed time, and numeric
   result. Log the resolved address only for the non-sensitive bench endpoint.
4. Start one GET with a persistent request context and callback table.
5. Validate a 2xx status, expected content type, supported framing, and size
   limits before accepting the body.
6. Stream chunks to the bounded consumer and maintain checked byte accounting
   plus a deterministic CRC32 for bench comparison.
7. Wait for terminal completion, then require the HTTP client to report idle
   before calling `W6X_WiFi_Disconnect(1)`.
8. On timeout, request cancellation and wait a second bounded interval for
   socket/task cleanup. Preserve the original timeout as the primary result.
9. Run the existing disconnect and selected teardown path even after fetch
   failure.

Acceptance check: one button-triggered lifecycle resolves the LAN host,
receives the expected status/body length/CRC, closes the HTTP socket, then
passes the existing disconnect and cleanup checks.

### Increment 4.4 - Deterministic local endpoint matrix

Use a local test server that can return exact scripted responses. Keep the
server outside firmware and do not commit private LAN addresses. Required
routes are:

| Route | Behavior | Expected firmware result |
|---|---|---|
| `/ok-fixed` | 200, expected type, small `Content-Length` body | Success |
| `/ok-close` | 200, no length, close-delimited body | Success |
| `/empty` | 204 or zero-length 200 | Defined success policy |
| `/not-found` | 404 body | `http-status` failure |
| `/wrong-type` | 200 with unexpected content type | `unexpected-content-type` |
| `/large-declared` | Declared length above limit | Fail before body delivery |
| `/large-streamed` | No length and body crosses limit | Bounded oversize failure |
| `/slow-header` | Delay before headers | Header/total timeout |
| `/slow-body` | Stall between body chunks | I/O/total timeout |
| `/truncated` | Close before declared length | Invalid/truncated response |
| `/malformed` | Invalid status or headers | `invalid-response` |
| `/chunked` | Valid chunked body | Decode or explicit unsupported result |

For success routes, use payloads that cross receive-buffer boundaries and
include embedded zero bytes only if the selected response contract permits
binary data. The consumer must use explicit lengths, never C-string
termination.

Acceptance check: every route reaches its expected authoritative result and
returns to a disconnected, reusable persistent lifecycle boundary.

### Increment 4.5 - Stress and resource validation

1. Add an explicit HTTP persistent stress mode or temporarily extend the
   existing persistent iteration with fetch.
2. Run 100 connect/DHCP/DNS/fetch/disconnect cycles with one W6X/Wi-Fi/LwIP
   initialization and one final teardown.
3. Record per-cycle fetch bytes, status, elapsed time, free heap, minimum-ever
   heap, active task count, pbuf count, and HTTP active/idle state.
4. Record high-water marks for `St67Service`, the HTTP worker, DebugService,
   SPI, modem, netif, and TCP/IP tasks.
5. Require no trend in post-cycle free heap, no task-count growth, no pbuf or
   socket left active, and no response checksum mismatch.
6. Repeat a smaller failure/recovery batch alternating success with timeout,
   oversize, malformed, and connection-refused responses.
7. Perform one single-full-shutdown fetch for functional coverage, but keep
   its resource result attributed to the open Phase 3 cold-restart issue.

Acceptance check: 100/100 persistent fetch cycles pass with stable resources,
and every injected failure is followed by a successful request without host
reset.

### Increment 4.6 - Production handoff contract

After transport stress passes:

1. Replace the CRC-only consumer with a fixed-capacity response object or a
   streaming parser interface owned by the application layer.
2. Define whether partial data is discarded or delivered with an explicit
   failure result. Default to discard.
3. Transfer ownership only after status, type, framing, size, and complete-body
   checks pass.
4. Ensure malformed application data cannot keep Wi-Fi connected or suppress
   cleanup.
5. Keep domain-specific parsing outside HTTP callbacks and outside the ST67
   lifecycle owner when practical.

Acceptance check: a validated immutable, length-delimited response reaches the
consumer exactly once; failed or partial responses are never presented as a
successful fetch.

## 8. Timeout and cancellation policy

Every wait must be bounded independently and by the total transaction
deadline:

| Stage | Bound | Cancellation action |
|---|---:|---|
| DNS | `APP_ST67_DNS_TIMEOUT_MS` | Invalidate request generation; ignore late callback |
| TCP connect | `APP_ST67_TCP_CONNECT_TIMEOUT_MS` | Close/shutdown socket |
| Send/header/body I/O | `APP_ST67_HTTP_IO_TIMEOUT_MS` | Close/shutdown socket |
| Whole fetch | `APP_ST67_HTTP_TOTAL_TIMEOUT_MS` | Cancel worker and wait for idle |
| Worker cleanup | Small explicit cleanup bound | Mark cleanup failure; do not disconnect underneath live socket |

The total deadline begins before DNS. Per-operation timeouts may not extend it.
Use wrap-safe tick comparisons. Socket timeout errors must remain distinct
from orderly peer close.

If cancellation does not reach HTTP idle within its cleanup bound, preserve
the original transaction failure but also report a cleanup failure. Do not
continue normal disconnect/teardown under a live worker; force the best proven
network cleanup path and require host reset if ownership cannot be recovered.

## 9. Response acceptance policy

The first production-capable HTTP path should enforce:

- HTTP/1.1 or explicitly supported HTTP/1.0 status line;
- status in the configured accepted 2xx set;
- header bytes no greater than `APP_ST67_HTTP_MAX_HEADER_BYTES`;
- no obsolete folded headers;
- case-insensitive header names and media type comparison;
- `Content-Type` match while allowing validated parameters such as charset;
- exactly one supported body framing mode;
- body bytes no greater than `APP_ST67_HTTP_MAX_RESPONSE_BYTES`;
- received length equal to valid `Content-Length` when present;
- orderly close for a close-delimited body;
- no body accepted after consumer failure or size violation.

Do not support gzip/deflate in the initial GET. The request should omit
`Accept-Encoding` or explicitly request `identity`. Reject an encoded response
unless a bounded decoder is deliberately added.

## 10. Error handling and observability

Stable failure stages should include:

```text
fetch-config
dns-start
dns-wait
tcp-connect
http-start
http-headers
http-status
http-content-type
http-framing
http-body
http-oversize
http-timeout
http-cancel
http-cleanup
```

One concise final record should contain:

- cycle and request identifiers;
- result stage and numeric detail;
- DNS, connect, first-byte, and total elapsed times;
- HTTP status;
- declared and received byte counts;
- response CRC or another non-sensitive integrity value;
- free/minimum heap, task count, pbuf count, and HTTP idle state;
- disconnect and teardown outcome without overwriting the first failure.

Do not log response bodies by default. Never log credentials, authorization
headers, cookies, query secrets, private certificate material, or a complete
sensitive URL.

## 11. Validation matrix

### Functional

- DNS cache miss and cache hit.
- Fixed-length body smaller than, equal to, and spanning receive chunks.
- Close-delimited body.
- Empty successful response according to the selected endpoint contract.
- Non-2xx status.
- Wrong or missing content type.
- Maximum accepted response and one byte over the limit.
- Server closes before declared length.
- Unsupported chunked or encoded response.

### Timing and network failure

- Unknown hostname and DNS server unavailable.
- Connection refused and unroutable destination.
- Delayed connect, header, and body.
- AP disappears during DNS, connect, header, and body.
- Disconnect trigger while fetch is active is rejected or deferred.
- Late callback from an expired request cannot affect the next request.

### Resource and recovery

- 100 successful persistent fetch cycles.
- Alternating success/failure recovery batch.
- Maximum-size response repeated enough to expose heap fragmentation.
- Debug USB traffic concurrent with maximum response traffic.
- No heap trend, task growth, pbuf leak, socket leak, callback-after-free, or
  stack overflow.

## 12. HTTPS follow-on track

Do not treat port 443 support in the generated client as production TLS. The
current build does not define `MBEDTLS_CONFIG_FILE`, and the source currently
configures no CA certificate.

Before enabling HTTPS:

1. Add a package-compatible mbedTLS 3.6.x configuration containing only the
   required TLS 1.2/1.3, X.509, cipher, hash, and key algorithms.
2. Choose a trust policy: a pinned private CA for a controlled endpoint or a
   maintained public CA set. Never accept an unauthenticated server.
3. Require hostname verification against the configured host.
4. Provide a cryptographically appropriate entropy source and document its
   startup behavior.
5. Establish certificate-time validation using a retained RTC or a documented
   authenticated bootstrap policy. Unauthenticated SNTP alone cannot establish
   trust for the first TLS connection.
6. Remove file-static per-connection TLS state or formally enforce and test
   one request at a time with complete context destruction.
7. Bound handshake, record I/O, total response, and TLS close operations.
8. Measure flash, static RAM, peak heap, and stack during handshake and the
   maximum response. Preserve at least the project's accepted runtime margin.
9. Test unknown CA, expired/not-yet-valid certificate, hostname mismatch,
   truncated handshake, server close, and repeated successful sessions.
10. Repeat the persistent 100-cycle resource test over HTTPS before selecting
    it for the daily fetch.

If authenticated HTTPS cannot meet memory and time requirements on the
STM32G0B1, record that result explicitly and reconsider endpoint/protocol or
hardware constraints. Do not weaken certificate validation to make it fit.

## 13. Expected file changes

| File or area | Planned change |
|---|---|
| `Appli/App/app_config.h` | Non-secret endpoint defaults, limits, timeouts, stress count |
| `LWIP/App/http_client.h/.c` | Bounded contract, framing fixes, completion/cancel/idle semantics |
| `User/Inc/HostController` | Fetch result/consumer API only if it must cross the service boundary |
| `User/Src/HostController` | DNS helper, transaction context, lifecycle integration, callbacks |
| `User/Src/HostController/St67ServiceTask.cpp` | Insert DNS/fetch before disconnect and preserve first failure |
| `Core/Inc/FreeRTOSConfig.h` | Heap change only if measurements justify it |
| `LWIP/Target/lwipopts.h` | DNS/socket/pool tuning only from measured failures |
| CMake files | Add mbedTLS only in the HTTPS track |
| `docs/ST67_Daily_Fetch_Implementation_Plan.md` | Record bench results and final HTTP/HTTPS decision |

Avoid changing generated or vendor code outside user-maintained integration
files unless the defect is owned there and the change is documented for
regeneration/package upgrades.

## 14. Build and bench validation

Build both firmware variants after every implementation increment:

```bash
"$CUBE_CMAKE" --build build/Debug-HostController
"$CUBE_CMAKE" --build build/Debug-DisplayController
git diff --check
```

Bench acceptance records should include endpoint behavior/version, firmware
mode, cycle counts, first failure, timing, bytes, CRC, heap/task/pbuf trends,
stack margins, and debug transport counters. Do not record private endpoint
credentials or response data.

## 15. Exit criteria

Plain HTTP Phase 4 is complete when:

- DHCP-gated host DNS and HTTP GET are integrated before disconnect;
- all transaction and cleanup waits are bounded;
- response status, type, framing, and size are validated;
- the response is streamed without allocation based on remote length;
- exactly one authoritative completion is produced after socket/task cleanup;
- the deterministic endpoint matrix passes;
- 100 persistent connect/DHCP/DNS/fetch/disconnect cycles pass with stable
  heap, task count, pbuf count, sockets, and stack margins;
- failure injection is followed by successful recovery without host reset;
- both Debug variants build and no secrets appear in tracked files or logs;
- the master plan records whether production proceeds to HTTPS.

HTTPS is complete only after authenticated hostname-verified TLS passes its
own resource, certificate-failure, and 100-cycle stability gates. Daily RTC
scheduling and application data processing remain Phase 5.
