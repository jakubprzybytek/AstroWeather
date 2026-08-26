# CubeMX Compliance Migration Plan

## Goal

Restore the customized HostController behavior after fresh STM32CubeMX generation while respecting CubeMX ownership rules.

The governing rule is:

- Application code belongs in `User/Inc` and `User/Src`.
- Generated files may contain custom code only inside existing `USER CODE BEGIN` / `USER CODE END` regions.
- Generated and vendor files must not be modified elsewhere.
- `External/Clean` and `External/Customized` are read-only references for this migration.

The first milestone is a compiling, operational baseline. Memory-leak investigation and cold-restart optimization are intentionally deferred.

## ST Documentation Basis

This plan is consistent with the following ST documentation:

- [X-CUBE-ST67W61 Architecture](https://wiki.st.com/stm32mcu/wiki/Connectivity:X-CUBE-ST67W61_Architecture)
- [How to use X-CUBE-ST67W61 STM32CubeMX pack](https://wiki.st.com/stm32mcu/wiki/Connectivity:Wi-Fi_How_to_use_X_CUBE_ST67W61_STM32CubeMx_pack)
- [ST67W6X Echo Application](https://wiki.st.com/stm32mcu/wiki/Connectivity:Wi-Fi_ST67W6X_Echo_Application)

The project selects ST67 architecture T02 with lwIP 2.2.1 on the STM32 host. ST documents that, for T02:

- lwIP owns TCP/IP and BSD sockets on the host;
- the ST Net and HTTP services are disabled;
- HTTP must be implemented on top of lwIP;
- the netif layer carries lwIP station and soft-AP traffic through `spi_iface`;
- `W61_MAX_SPI_XFER` must be 1520 bytes to avoid wasting dynamic memory.

This directly supports placing the application HTTP implementation under `User` rather than customizing the generated HTTP client. The project already configures `W61_MAX_SPI_XFER=1520U`.

ST also states that X-CUBE-ST67W61 USER CODE section coverage is intentionally limited and may not cover every customization. This reinforces the project rule that substantial application behavior belongs under `User/Inc` and `User/Src`.

## Current State

The current tree is effectively the freshly generated `External/Clean` state. The previous customized state is represented by `External/Customized`.

The regeneration removed custom lifecycle and HTTP changes. Surviving User code still references APIs that are no longer present:

- `HTTP_Client_Cancel()` and `HTTP_Client_IsIdle()`
- `MX_LWIP_DeInit()`
- `lwip_get_station_status()` and `lwip_netifs_are_removed()`
- `net_if_deinit()`, `net_if_is_running()`, and `net_if_outstanding_pbufs()`

SPI1 RX/TX DMA configuration was removed by the earlier regeneration and has now been restored through CubeMX. The regenerated configuration uses DMA1 Channel 1 for SPI1 RX and DMA1 Channel 2 for SPI1 TX. Both channels use high-priority, byte-aligned, normal-mode transfers with peripheral increment disabled and memory increment enabled. DMA initialization runs before SPI1 initialization, and both IRQ handlers dispatch to the correct HAL DMA handles at interrupt priority 3.

This matches ST's documented requirement for full-duplex master SPI with high-priority TX and RX DMA, memory increment enabled, and peripheral increment disabled. `ST67_RDY` is configured for rising and falling edge interrupts as documented. The current SPI kernel clock is 16 MHz with a prescaler of 16, producing a 1 MHz SPI clock, safely below ST's 40 MHz maximum.

The FreeRTOS heap is configured to 40,000 bytes, matching ST's minimum recommendation for a project generated from scratch.

The top-level `CMakeLists.txt` already discovers shared and variant-specific sources under `User/Src` and includes `User/Inc`. New User modules should therefore be picked up without modifying generated CMake files.

## Migration Inventory

| Area | Required action | Safe destination |
| --- | --- | --- |
| Application bootstrap | Retain `AstroWeather_Init()` and `AppVariant_Init()` integration | Existing USER blocks in `Core/Src/main.c`; implementations in `User` |
| USB debug | Retain the CDC bridge to `DebugService_OnUsbRxData()` | Existing USER blocks in `USB_Device/App/usbd_cdc_if.c`; implementation in `User` |
| SPI DMA | Complete: RX/TX DMA, DMA IRQs, and priority 3 regenerated and compile-checked | CubeMX `.ioc` configuration followed by regeneration |
| SPI task stack | Use the larger ST67 stack requirement | Target compile definition in top-level `CMakeLists.txt` |
| CM0+ FreeRTOS compatibility | Provide `xPortIsInsideInterrupt()` as documented by ST for FreeRTOS older than 10.6 | Existing USER configuration section or User-owned compatibility header included from a USER block |
| SPI completion and RDY handling | Expose User-owned bridge functions and call them from generated hooks | `User/Src` bridge; minimal calls in existing USER blocks in `spi_port.c` and generated GPIO callback code |
| Station status | Replace removed custom status APIs | New adapter under `User/Inc/HostController` and `User/Src/HostController` using public lwIP interfaces |
| HTTP request ownership | Replace dependencies on customized generated HTTP state | New User-owned HTTP wrapper/worker, then update `St67HttpFetcher.cpp` |
| Network lifecycle | Stop depending on private full teardown | Use persistent lwIP/ST67 ownership for the initial compliant implementation |
| Cold restart | Defer until supported teardown is available | Revisit only after public APIs or a fully User-owned replacement are identified |

## Phase 1: Restore the Generated Hardware Baseline

1. Complete: update the CubeMX configuration to restore SPI1 RX and TX DMA.
2. Complete: restore DMA interrupt generation at priority 3.
3. Complete: regenerate the project.
4. Complete: verify the generated DMA mapping, HAL links, initialization order, and IRQ dispatch.
5. Add the ST-documented `xPortIsInsideInterrupt()` compatibility definition for this Cortex-M0+ and FreeRTOS 10.3.1 configuration. This must be placed in a preserved USER section or User-owned compatibility header, not in vendor `spi_iface.c`.
6. Add `SPI_THREAD_STACK_SIZE=1536U` as a compile definition for the HostController target in the top-level `CMakeLists.txt`. ST documents this as an overrideable driver setting; 1536 bytes is the project-specific value retained from the previously working configuration.
7. Build the HostController and DisplayController variants before restoring higher-level behavior.

Do not manually recreate DMA handles, MSP links, IRQ forwarding, or peripheral initialization in generated files.

The DMA-specific generated units compile successfully. A fresh `Debug-HostController` build currently proceeds past configuration but fails in surviving custom code because removed HTTP and network lifecycle APIs are still referenced. It also reports a missing declaration for `__get_IPSR` in `spi_iface.c`; ST's documented CM0/CM0+ `xPortIsInsideInterrupt()` compatibility definition is the intended fix. These failures are separate from the generated DMA configuration.

## Phase 2: Remove Compile-Time Dependencies on Generated Extensions

### Station status adapter

Add a small User-owned adapter that combines the public `W6X_WiFi_Station_GetState()` API with `netif_get_interface(NETIF_STA)` and supported lwIP netif fields/accessors. ST documents both station-state polling and asynchronous connected, disconnected, and got-IP events.

Update `St67NetworkSession.cpp` to use this adapter instead of:

- `lwip_get_station_status()`
- `lwip_netifs_are_removed()`
- `net_if_is_running()`
- `net_if_outstanding_pbufs()`
- `MX_LWIP_DeInit()`

The adapter must not reach into private generated state.

### Persistent lifecycle policy

Change the default application lifecycle away from `APP_ST67_LIFECYCLE_SINGLE_FULL_SHUTDOWN`. Keep the network stack initialized and support:

1. connect or wait for DHCP,
2. fetch data,
3. disconnect or release the session without destroying the stack,
4. repeat the operation.

This is the smallest compliant path because the previous full teardown depended on private state in generated and vendor modules.

## Phase 3: Move HTTP Behavior Behind a User-Owned Boundary

Do not restore the customized implementation directly into `LWIP/App/http_client.c`. Most of that customization was outside CubeMX USER blocks and will be overwritten again.

Add a User-owned HTTP implementation with the minimum behavior needed by `St67HttpFetcher`:

- one active request at a time,
- request cancellation,
- bounded request and response buffers,
- connect and receive timeouts,
- response-header and status validation,
- safe `Content-Length` handling,
- protection against oversized or malformed responses,
- exactly-once completion/error notification,
- cleanup of task, socket, TLS state, and allocated buffers on every path.

Then update `St67HttpFetcher.cpp` to call the User-owned API rather than `HTTP_Client_Cancel()` and `HTTP_Client_IsIdle()`.

The initial implementation may support only the HTTP method and transport required by the application. HTTPS and less-used POST/PUT behavior should be added only when required by an actual caller.

## Phase 4: Restore Callback Bridges

Keep all substantive behavior in User-owned source files. Generated callbacks should only make minimal calls from existing USER blocks.

Expected bridges include:

- SPI transaction completion to the ST67 User-owned transport state.
- GPIO RDY rising edge to the User-owned ST67 notification path.
- Optional recoverable SPI error notification.

The generated `spi_port.c` already provides USER blocks around SPI completion and error callbacks. Do not add duplicate HAL callback definitions outside those hooks.

## Explicitly Deferred

The following customized behavior should not be migrated in the initial pass because it requires prohibited changes to generated or vendor internals:

- full `MX_LWIP_DeInit()` behavior,
- private netif and timer cleanup,
- W61 initialization failure cleanup inside vendor code,
- private SPI error-state propagation,
- customized HTTP internals inside the generated HTTP client.

ST does provide public `W6X_WiFi_DeInit()`, `W6X_Netif_DeInit()`, and `W6X_DeInit()` APIs. These should be used where appropriate, but they do not deinitialize the CubeMX-generated lwIP timers, threads, and netifs. Full cold-restart support can therefore be reconsidered only if the remaining lwIP teardown can be performed through supported public APIs or if that layer is replaced entirely by a maintained User-owned module. A wrapper is insufficient when the required state is `static` inside generated files.

## Instrumentation To Omit

Do not migrate memory-leak investigation code at this stage:

- heap, task, and pbuf checkpoint logging,
- lwIP netif/timer/AP-table allocation counters,
- Wi-Fi context and event allocation counters,
- modem semaphore and buffer counters,
- command-handler mutex counters,
- SPI queue/event allocation counters,
- allocation summaries added to deinitialization functions,
- repeated cold-restart stress instrumentation.

Retain only operational safeguards needed for correctness, especially buffer bounds, timeout handling, and deterministic cleanup.

## Validation Gates

After each CubeMX regeneration:

```text
git diff --check
git diff --stat
git diff --name-status
git diff -- HostControllerA.ioc Core cmake/stm32cubemx
```

Build gates:

1. `Debug-HostController`
2. `Debug-DisplayController`
3. `Release-HostController`

The configured build toolchain is GNU Arm Embedded with the Ninja generator. The current STM32Cube bundle provides CMake, Ninja, and the compiler on `PATH`:

- CMake: `C:\Users\jakub.przybytek\AppData\Local\stm32cube\bundles\cmake\4.3.1+st.1\bin\cmake.exe`
- Ninja: `C:\Users\jakub.przybytek\AppData\Local\stm32cube\bundles\ninja\1.13.2+st.1\bin\ninja.exe`
- GNU Arm: `C:\Users\jakub.przybytek\AppData\Local\stm32cube\bundles\gnu-tools-for-stm32\14.3.1+st.2\bin\arm-none-eabi-gcc.exe`

Build a preset with:

```text
cmake --build --preset Debug-HostController
```

Runtime gates:

- SPI DMA transfers complete and their interrupts reach the driver.
- DHCP and station-status transitions are observed through public interfaces.
- Repeated persistent connect/fetch/disconnect cycles do not create tasks or sockets indefinitely.
- HTTP success, timeout, malformed header, oversized response, and unreachable-host cases complete exactly once.
- HTTP buffers and sockets are released on success, cancellation, timeout, and error.
- The optional probe task never runs concurrently with the official driver.

Do not make cold-restart heap stability a release gate until a compliant teardown implementation exists.

## Risks and Decisions

- The current default full-shutdown lifecycle is incompatible with the smallest safe migration path.
- The generated HTTP client does not provide the customized ownership and cancellation contract required by the current User code.
- The existing build artifacts may contain a stale compile database; a fresh CMake build is required for trustworthy diagnostics.
- The STM32Cube toolchain is now available on `PATH`; fresh builds should be used to replace stale build-artifact diagnostics.