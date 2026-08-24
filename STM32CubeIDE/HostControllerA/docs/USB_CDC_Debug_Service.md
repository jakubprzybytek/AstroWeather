# USB CDC Debug Service

## Purpose and Scope

The HostController firmware exposes a USB CDC virtual COM port for diagnostic logging and a line-oriented host healthcheck. `DebugService` owns the application-facing CDC behavior: it serializes transmit requests, accepts logs from application tasks, and converts received USB bytes into echo responses.

The service is currently started only by the HostController variant. It is not started by the DisplayController variant. The service is intentionally limited to logging and the healthcheck echo; it is not a general command-line interface and does not persist logs.

## Lifecycle

`AppVariant_Init()` initializes and starts the singleton service after the RTOS kernel has been initialized. `DebugService` is a `Task<1024>` named `DebugService` with normal priority.

USB device startup remains in the CubeMX USB device layer. `CDC_Transmit_FS()` treats an uninitialized or disconnected USB CDC class as busy, allowing the debug task to continue operating while no host is attached.

## Module Boundaries

- `User/Inc/Debug/DebugService.hpp` defines the C++ service and its logging API.
- `User/Src/Debug/DebugService.cpp` implements the service and the C bridge function.
- `User/Inc/Debug/DebugServiceBridge.h` exposes `DebugService_OnUsbRxData()` to C code without exposing C++ types.
- `USB_Device/App/usbd_cdc_if.c` remains the CubeMX CDC transport implementation. Its receive callback passes bytes to `DebugService_OnUsbRxData()` and immediately rearms reception.
- `User/Src/HostController/AppVariant.cpp` is the HostController-only startup point.

Keep CDC-specific changes in CubeMX `USER CODE` sections. Application code must not call `CDC_Transmit_FS()` directly: the `DebugService` task is the sole application owner of CDC transmission.

## Logging API

Application tasks publish diagnostics through the singleton:

```cpp
DebugService::instance().log(DebugService::Level::Info, "connected");
DebugService::instance().logf(DebugService::Level::Warn, "retry %lu", retryCount);
```

`Level` values are `Info`, `Warn`, `Error`, and `Debug`, emitted as `INFO`, `WARN`, `ERR`, and `DEBUG`. When viewed in an ANSI-capable terminal, error records are red, warnings are yellow, and debug records are dark gray. Info records have no color. Each log line is formatted as:

```text
[days:hours:minutes:seconds] [LEVEL] message\n
```

The uptime is derived from `HAL_GetTick()`. `log()` and `logf()` are non-blocking, task-context APIs. They copy the formatted result into a static CMSIS message queue; callers must not retain ownership of a buffer after publishing because no caller buffer is stored.

The queue holds 64 entries of up to 127 characters plus a terminator. Formatting that exceeds the fixed record is truncated by the C formatting functions. On a full queue, the oldest queued record is removed and the newest record is inserted. If the replacement cannot be inserted, the new record is dropped. In either case, `droppedCount` is incremented for each lost record.

## Receive and Healthcheck Protocol

`CDC_Receive_FS()` may run in USB interrupt context. It passes the received bytes to the service, which places them in a single-producer/single-consumer ring buffer and wakes the debug task. The ring holds 256 bytes. Bytes received after the ring is full are discarded and counted in `rxOverflowCount`.

The debug task assembles lines as follows:

- `\r` is ignored.
- `\n` terminates a line.
- Other bytes are treated as text.
- A line stores at most 95 characters. Further characters before its terminator are discarded; one truncation is counted in `rxTruncatedCount` for that line.

Every completed line, including an empty line, produces this echo response:

```text
[days:hours:minutes:seconds]: received text\n
```

For example, `ping\n` sent by the host can produce `[0:00:01:23]: ping\n`. The host must send a newline to receive a response. The service does not interpret commands.

## Transmit Behavior and Backpressure

The debug task serializes healthcheck echoes, log records, and periodic statistics through a shared transmit buffer. It retries `CDC_Transmit_FS()` only when the transport reports `USBD_BUSY`, waiting 5 ms between attempts for no more than 40 ms. A non-busy failure or an exhausted retry window drops that outgoing packet and increments `busyDropCount`.

Queued logs are removed before they are transmitted. Therefore, a log accepted by `log()` can still be lost if CDC transmission later fails or remains busy. Echo responses use the same retry policy. This design favors keeping application tasks responsive over retaining output while the host is absent or slow.

When neither incoming data nor queued logs wake the task, it times out after 5 seconds and emits:

```text
[days:hours:minutes:seconds] [STATS] sent=N dropped=N busyDrop=N rxOverflow=N rxTrunc=N\n
```

The periodic `[MEM]` and `[STACK]` records use the same dark-gray ANSI formatting as `Debug` logs. The `[STATS]` record remains uncolored.

`sent` counts successfully transmitted log records only; it does not include echo responses or statistics lines. The other counters are cumulative since startup.

## Fixed Limits

| Resource | Limit |
| --- | ---: |
| Debug task stack | 1024 bytes |
| Log queue depth | 64 records |
| Log record text | 127 characters plus terminator |
| RX ring | 256 bytes |
| RX line text | 95 characters plus terminator |
| CDC busy retry window | 40 ms |
| CDC busy retry delay | 5 ms |
| Statistics interval | 5 seconds of inactivity |

## Maintenance Notes

- Preserve the C bridge between `usbd_cdc_if.c` and the C++ service. Do not include C++ headers in CubeMX-generated C code.
- Preserve the single CDC transmit owner. Any future CDC output type should enter the service task rather than calling the transport from another task or interrupt.
- If logging from interrupt context is required, add a dedicated ISR-safe producer path. The existing logging API is documented for task context; only the USB RX ingress path is designed for interrupt context.
- Changes to queue, ring, or line limits affect static memory use and loss behavior. Update this document and validate burst logging, long input lines, and host disconnect/reconnect behavior with the affected build variant.
