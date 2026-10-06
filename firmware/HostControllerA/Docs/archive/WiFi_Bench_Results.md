> Archived 2026-10-06. Current state: [WiFi.md](../WiFi.md).

# WiFi Bench Results

The bench record of the ST67 work, from the raw AT probe in August 2026 to the
device key in October, moved out of [WiFi.md](../WiFi.md).

## Bench results

All runs used the first board. Details are in the
archived phase plans; the heap figures before 2026-08-26 are from the tree with
the customized LwIP teardown, since removed.

| Phase | Date | Result |
| --- | --- | --- |
| 0.1 Raw AT/CWLAP probe | 2026-08-21 | SPI framing, CS/RDY handshake and multi-frame AT replies work; scans returned 0 to 19 APs. [Daily fetch plan](ST67_Daily_Fetch_Implementation_Plan.md) |
| 1 SPI DMA | 2026-08-21 | 100 DMA init/transfer/deinit cycles, no timeouts or HAL errors, heap flat at 39 080 B. |
| 2 Official driver | 2026-08-21 | `W6X_Init`, WiFi and LwIP init and a scan (20 APs) passed; a HardFault in the scan was fixed by raising the SPI engine stack from 768 to 1536 B. [Phase 2](ST67_Phase_2_Implementation_Plan.md) |
| 3 Join/DHCP/disconnect | 2026-08-22..23 | 100/100 persistent cycles, heap flat at 23 984 B, min 21 552 B. 20/20 cold restarts passed but free heap fell from 33 896 to 27 864 B: not resource-stable. Wrong password, no credentials and AP-off all failed cleanly and recovered. [Phase 3](ST67_Phase_3_Implementation_Plan.md) |
| 4 HTTP fetch | 2026-08-23..24 | First GET: HTTP 200, 83 B. 100/100 `HttpPersistentStress` cycles, heap flat, min 14 680 B. The client-owned buffer hand-off (`FetchSt67Data`) validated, CRC matched. [Phase 4](ST67_Phase_4_Implementation_Plan.md) |
| CubeMX regeneration | 2026-08-26 | With the User-owned `HttpClient`, adapter and RDY bridge: smoke test, 100/100 persistent and 100/100 HTTP persistent cycles, min heap 18 944 B, `St67HttpFetch` 840 B stack left. [CubeMX_Compliance_Migration.md](CubeMX_Compliance_Migration.md) |
| Driver priorities | 2026-09-21 | Longest display slot gap during WiFi fell from 14 ms to 8 ms with fetches still succeeding. |
| Connect diagnosis | 2026-09-21 | A wrong WPA2 password reported reason 7 and was classified `WrongPassword`. |
| T01 and HTTPS | 2026-10-03..04 | Host rewritten against `W6X_Net`; `.bss` 91 808 B against 138 412 B under T02. Module programmed with `mission_t01_v2.0.106` through `firmware/Bypass`. Three faults found and fixed on the bench: `W6X_Net_Init()` asserting without a registered net callback; the station reporting `GOT_IP` straight after the join; the driver's file listing of ST's 31 sample certificates overrunning its 2 s timeout before every certificate upload (fixed by programming a LittleFS image holding only Amazon Root CA 1, `Bypass/tools/Build-LittleFS.sh`). The driver's AT trace also overflowed the 2560 B fetch stack, now 4096 B. **First HTTPS fetch passed**: DNS 0.3 s, certificate upload 3 s (first time only), TLS handshake with CloudFront about 1 s, HTTP 200, 1906 bytes, CRC valid, parse OK; `heapMin` 24 368 B. Certificate cases on a bench build trusting ISRG Root X1 (`-DAPP_ST67_TLS_BENCH_ANCHOR_ISRG=ON`, badssl.com hosts): wrong CA, hostname mismatch, untrusted root, self-signed and an ISRG Root X2 chain all refused in the handshake; `sha256`/`rsa2048.badssl.com` completed with HTTP 200. `wifi stress`: **100/100 HTTPS cycles** in 21.5 min, free heap 29 384 B after every cycle, `heapMin` 24 368 B throughout, 13 tasks; after `stop()` 38 840 B free and a `wifi test` restarted the module and fetched. Repeated with the heap at 32 000 B: 99/100 (one transient connect failure, recovered), `heapMin` 16 336 B. Details in the plan's bench record. The date check is left open. |
| API without CloudFront | 2026-10-04 | The API moved from CloudFront to an API Gateway custom domain, HTTPS only (`sst/docs/architecture.md`). Same ACM chain to Amazon Root CA 1, so the firmware is unchanged; build from `main` at `64e3b54`. On `int`: `astro refresh` (module cold start) and `wifi test` each fetched `/astro/wroclaw` with HTTP 200, 1906 bytes, CRC valid, parse OK, about 10 s and 8 s; free heap 21 384 B, `heapMin` 16 560 B. Before the router was restarted it kept serving the deleted CloudFront addresses for over 30 min after the DNS change, with a fresh TTL each time; the device resolves through it, so after a hostname move check its answer before testing. |
| Device key | 2026-10-04 | The API now needs credentials; the device uses `GET /device/astro/{id}?key=<key>` with the stage's `DeviceApiKey` (`sst/docs/architecture.md#access-control`), set in `APP_ST67_HTTP_PATH`, no code change. On `int`: `astro refresh` and `wifi test` each fetched `/device/astro/wroclaw?key=...` with HTTP 200, 1906 bytes, CRC valid, outcome ok; heap unchanged (21 384 B free, `heapMin` 16 560 B). The key was at first part of the path, so `api show` and the fetch log printed it. |
| Key in EEPROM | 2026-10-04 | `api key` saves the key as setting `0x14` (`ApiKey`); the fetch appends `?key=` and logs only `key=<set>`. On `int`, built-in path `/device/astro/wroclaw` and no built-in key: without a key the fetch got HTTP 401; after `api key` it got 200, 1906 bytes, CRC valid. After a reset `api show` reported `key=<set> (saved)`, and `astro test` (`/device/astro/test`, 1747 bytes) and `astro refresh` both returned 200. |

## Task priority history

The driver's two tasks default to 53 and 54. They were overridden to 46 and 47
to stay below the display multiplexing task `DisplayRefresh`, then at
`osPriorityRealtime` (48), after they held a display slot for up to 14 ms
during WiFi activity. Under T02 the generated LwIP `netif` task ran at 50, a
value that could not be overridden, and held the display off for up to 5.4 ms,
so `DisplayRefresh` was raised to `osPriorityRealtime7` (55). The display
refresh has since moved into the TIM2 interrupt and competes with no task.
The LwIP `tcpip_thread` (4096 B) and `netif` task (2048 B) went with T02 on
2026-10-03. The fetch task's stack was 2560 B until 2026-10-04, when the
driver's AT trace overflowed it.

## Why the lifecycle became persistent

The original plan was to shut the module down (`CHIP_EN` low, about 200 nA)
between daily fetches. Under T02 that needed a full LwIP teardown, which the
generated code did not provide, and even with a customized one 20 cold
restarts lost about 6 KB of heap. Keeping everything initialized passed 100
cycles with a flat heap. On 2026-10-04, under T01, a `wifi test` after the
stress batch's `stop()` re-initialised the driver and fetched normally, with
the heap back to 38 840 B of the 39 080 B at boot in between.
