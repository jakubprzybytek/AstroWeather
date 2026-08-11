## Plan: USB CDC Debug Service Architecture

Build a dedicated USB debug service under User/.../Debug that centralizes CDC access, provides task-safe logging from multiple producers, and handles line-based host healthcheck echo with uptime timestamp. The service runs as a Task<>-based RTOS thread and uses queue + synchronization primitives to avoid races and USB busy drops while keeping producer calls non-blocking.

## Steps

1. Phase 1 - Define module boundaries and contracts.
2. Add a debug service API in User/Inc/Debug for: initialization/start, log publish from any task, CDC RX ingress callback handoff, and line-oriented command/healthcheck handling.
3. Keep USB low-level transport in USB_Device/App/usbd_cdc_if.c and route received bytes into the debug service via a single extern callback bridge.
4. Mark this architecture HostController-only for now by starting the debug service from User/Src/HostController/AppVariant.cpp.
5. Phase 2 - Task-safe data flow and synchronization design.
6. Implement one dedicated consumer task (Task<...>) that is the only caller of CDC_Transmit_FS; this avoids multi-task contention on USB TX state.
7. Use a static FreeRTOS/CMSIS message queue for log events produced by any task. Producer API is non-blocking and copy-based.
8. On queue-full policy, drop oldest then enqueue newest. Maintain counters for dropped, sent, busy-retry, and rx-overflow diagnostics.
9. Use an event flag or task notification to wake the debug task on new RX data and queued log events; avoid 100 ms polling latency.
10. Protect shared RX assembler state with lightweight synchronization appropriate for ISR/task crossing (critical section around ring-buffer head/tail updates or CMSIS ISR-safe queue APIs).
11. Phase 3 - Protocol behavior.
12. Implement line-based RX parsing with newline terminator. Each complete line from host triggers an echo response: [mm:ss]: <line>\n, using HAL_GetTick() uptime.
13. Keep host healthcheck independent from logging path but serialized by same TX owner task so outbound packets are ordered and collision-free.
14. Sanitize RX payload to bounded text buffers and null-terminate only in local parsing buffer (never assume USB packet has C-string terminator).
15. Phase 4 - Integration with generated STM32 files.
16. Move app-level USB buffers/state out of Core/Src/main.c USER CODE section into the debug module; remove direct polling echo loop from StartDefaultTask.
17. Keep MX_USB_Device_Init() placement unchanged unless startup ordering issue is found; debug task should tolerate USB disconnected state and retry later.
18. In USB_Device/App/usbd_cdc_if.c, keep CubeMX-generated structure intact and only use USER CODE regions to call into debug RX ingress bridge.
19. Phase 5 - Verification and observability.
20. Add compile-time constants for queue depth, max log message size, max RX line length, and TX retry window; tune for configTOTAL_HEAP_SIZE and static memory constraints.
21. Validate with host scripts: newline healthcheck echo timing, burst logging from multiple tasks, disconnected USB then reconnect, and long-line truncation handling.
22. Expose optional periodic stats log (dropped count etc.) to confirm backpressure policy in real conditions.

## Relevant Files

- Core/Src/main.c: remove current USB echo state machine in USER CODE and delegate to debug service.
- USB_Device/App/usbd_cdc_if.c: retain CDC transport ownership and bridge RX callback to debug module in USER CODE sections.
- USB_Device/App/usbd_cdc_if.h: transport API surface used by debug service task for TX.
- User/Inc/Utils/Task.hpp: Task<> pattern to host new service task.
- User/Inc/Utils/TaskBase.hpp: thread attribute and lifecycle behavior constraints.
- User/Src/Utils/TaskBase.cpp: start/entrypoint behavior reused by debug service.
- User/Inc/Debug/BlinkingLed.hpp: style/template reference for Debug task class placement.
- User/Src/Debug/BlinkingLed.cpp: style/template reference for Debug task implementation placement.
- User/Src/HostController/AppVariant.cpp: HostController-only startup integration point.
- Core/Inc/FreeRTOSConfig.h: confirms available primitives (mutex, event flags from ISR, static allocation support).

## Verification

1. Build HostController preset and confirm no references remain to legacy main.c USB buffers and flags.
2. Send healthcheck lines from PC terminal: verify immediate newline-delimited echo with [mm:ss] uptime format.
3. Spawn two or more periodic producer tasks that emit logs concurrently: verify no interleaving corruption and deterministic ordering at sink.
4. Stress queue capacity by burst logging above service throughput: verify drop-oldest behavior and counters increment.
5. Force CDC_Transmit_FS busy situations (small inter-frame delays): verify retry/backoff path sends most messages without task blocking.
6. Disconnect and reconnect USB cable while tasks continue logging: verify system stability and resumed output after reconnect.
7. Confirm service is only started in HostController variant and absent in DisplayController variant.

## Decisions

- Scope: HostController variant only.
- Host command framing: line-based with newline terminator.
- Queue overflow policy: drop oldest and keep newest logs.
- Included scope: architecture for task-safe logging plus healthcheck echo over VCP.
- Excluded scope: full command interpreter CLI, persistent log storage, DisplayController enablement.

## Further Considerations

1. Recommended message envelope: prepend optional level or tag in producer API (INFO, WARN, ERR) now, even if host parser initially treats output as plain text.
2. Recommended TX policy: bounded retry (for example short retries over about 20 to 50 ms total), then drop with counter to avoid starvation under host disconnect.
3. If ISR-origin logs are needed later, add a dedicated ISR-safe lightweight API path (fixed-size record push) to avoid dynamic formatting in interrupt context.
