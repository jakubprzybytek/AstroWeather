# Display Implementation Plan

This plan implements the behavior defined in [Display.md](Display.md). Display code must remain under `User` so STM32CubeMX regeneration does not overwrite it.

## Current Baseline

The repository already provides:

- Shared C++ sources under `User/Src`; CMake includes them automatically.
- Variant-specific startup under `User/Src/HostController` and `User/Src/DisplayController`.
- Static FreeRTOS task support through `Task<StackSizeBytes>`.
- An SCT2xxx wrapper under `User/Inc/Device` and `User/Src/Device`.
- SPI3 configured as 8-bit, mode 0, MSB first.
- I2C1 configured on PA9/SCL and PA10/SDA.
- TIM2 configured from a 16 MHz internal clock with prescaler `15999`, period `3`, and a generated `TIM2_IRQHandler`, producing a 250 Hz update event when started.
- Active-low `DISPLAY_1_EN` through `DISPLAY_5_EN` GPIO outputs.
- Address inputs named `ADDR_0`, `ADDR_1`, and `ADDR_2` in generated code.

The following baseline gaps remain:

- SCT2xxx currently sends and latches one byte at a time; a display slot requires seven bytes followed by one latch pulse.
- TIM2 is not started and its elapsed callback is not connected to a display task.
- The Display Controller variant has no I2C target/slave setup or receive path.
- There is no host-side unit-test target. Pure display logic should be isolated from HAL so one can be added without compiling firmware dependencies.

Implementation status: the pure logical model, explicit I2C codec, PCB encoder, SCT multi-byte transfer, PCB refresh task, buffered boards, stable host submission ordering, TIM2 callback forwarding, and ternary address detector are implemented under `User`. A Display Controller I2C target adapter and variant composition remain pending the required CubeMX target-mode configuration.

## Proposed File Layout

Use the repository's existing include/source split while keeping all display functionality in the Display namespace/folder:

```text
User/
  Inc/
    Device/
      SCT2xxx.hpp
    Display/
      DisplayTypes.hpp
      DisplayBoard.hpp
      DisplayCodec.hpp
      DisplayI2cProtocol.hpp
      PcbDisplayBoard.hpp
      BufferedDisplayBoard.hpp
      Display.hpp
      DisplayAddress.hpp
  Src/
    Device/
      SCT2xxx.cpp
    Display/
      DisplayTypes.cpp
      DisplayCodec.cpp
      DisplayI2cProtocol.cpp
      PcbDisplayBoard.cpp
      BufferedDisplayBoard.cpp
      Display.cpp
      DisplayAddress.cpp
```

Variant composition remains in:

```text
User/Src/HostController/AppVariant.cpp
User/Src/DisplayController/AppVariant.cpp
```

Do not put application behavior in generated `Core` files. Peripheral and NVIC changes must be made in `HostControllerA.ioc` and regenerated. Use generated HAL callbacks or code inside existing `USER CODE` regions only where a C-to-C++ forwarding hook is unavoidable.

## Design Decisions

### Pending and submitted state

Every Display Board has a pending logical state containing:

- Four `NumericData` values.
- Five 21-bit matrix rows.

Calls to `setFixed()`, `setValue()`, `setTime()`, `setBlank()`, and `setRow()` update only pending state. They do not start I2C or alter the active SPI frame.

`Display::submit()` forms one application-level commit point:

1. Call the local PCB-backed board's `submit()`, which encodes its pending state into the prepared 35-byte SPI frame.
2. Call each buffer-backed board's `submit()`, which serializes and sends its pending state to the board's constructor-provided I2C address.

On a Display Controller, a valid received command replaces the pending logical state and invokes the local PCB-backed board's `submit()`.

### Board abstraction

Expose the same setters through a common `DisplayBoard` abstraction. The abstraction owns logical state and delegates submission behavior:

- `PcbDisplayBoard::submit()` encodes the logical state for local refresh.
- `BufferedDisplayBoard::submit()` serializes command `0x01` and transmits it over I2C.

Use static object ownership. Do not allocate boards or RTOS objects dynamically.

### Buffer formats

Keep three formats distinct:

1. Logical C++ state: typed values used by setters and encoders.
2. I2C wire payload: one command byte plus 27 explicitly serialized bytes.
3. SPI refresh frame: five slots of seven bytes, already encoded in SPI wire order.

Never transmit `NumericData` or another C++ struct by copying its object representation. Serialize fields explicitly to avoid padding and host-endian dependencies.

### SPI slot storage

Store every seven-byte prepared slot directly in SPI wire order:

1. Numeric display 4.
2. Numeric display 3.
3. Dot-matrix byte 3.
4. Dot-matrix byte 2.
5. Dot-matrix byte 1.
6. Numeric display 2.
7. Numeric display 1.

This removes reverse copying from the periodic refresh path. SPI remains MSB first; bits inside bytes are not reversed.

## Phase 1: Pure Logical Model

### Tasks

1. Add `DisplayTypes.hpp` and `DisplayTypes.cpp`.
2. Define constants for four numeric displays, five matrix rows, 21 matrix columns, five slots, seven bytes per slot, 27 payload bytes, and 28 command bytes.
3. Define `NumericMode`, flag masks/shifts, `NumericData`, logical board state, prepared slot/frame types, and I2C byte-array types.
4. Implement numeric setters or a `NumericDisplay` proxy that writes into a referenced `NumericData`.
5. Implement a matrix-row proxy that masks input to 21 bits.
6. Implement these conversions:
   - Integer to Value mode with precision zero.
   - Fixed mantissa plus precision to Value mode.
   - Float to rounded fixed mantissa plus precision.
   - Hour/minute to Time mode and `HHMM` mantissa.
   - Blank and Error modes.
7. Enforce fitting rules for sign, four display positions, and precision.
8. Invalid input stores Error mode and setters return no status.
9. Normalize negative zero to zero through ordinary numeric comparison/conversion; add no special display state.

### Validation

Add host-side tests for:

- Positive and negative boundary values.
- Precision 0 through 3.
- Values that cease to fit after rounding.
- NaN and infinity.
- Blank and Error flags.
- `setTime(3, 7)` becoming mantissa `307` in Time mode.
- Matrix values masking bits 21 through 31.

### Exit criteria

All public setters produce the expected canonical logical state without HAL or RTOS dependencies.

## Phase 2: I2C Codec

### Tasks

1. Add `DisplayI2cProtocol.hpp` and `.cpp` as pure code.
2. Define command `0x01` and exact message size constants.
3. Serialize the 27-byte payload in this order:
   - Numeric display 1: mantissa low, mantissa high, flags.
   - Numeric display 2: mantissa low, mantissa high, flags.
   - Matrix rows 0 through 4: three little-endian bytes per row.
   - Numeric display 3: mantissa low, mantissa high, flags.
   - Numeric display 4: mantissa low, mantissa high, flags.
4. Clear matrix bits 21 through 23 during serialization.
5. Deserialize only an exact 28-byte message with known command `0x01`.
6. Decode into temporary logical state and publish it only after the entire message validates.
7. Reject unknown commands, invalid modes/flags, and malformed lengths without changing destination state.

### Validation

Test exact byte arrays for positive and negative mantissas, all matrix boundary bits, command filtering, truncated input, invalid flags, and serialize/deserialize round trips.

### Exit criteria

Host and Display Controller can share one codec with byte-for-byte deterministic output and atomic decode behavior.

## Phase 3: PCB Encoder

### Tasks

1. Add `DisplayCodec.hpp` and `.cpp` as pure code.
2. Define normalized seven-segment masks for digits 0 through 9, minus, blank, and Error segment D.
3. Split Value and Time mantissas into four normalized digit positions.
4. Apply leading-blank behavior for Value mode and leading-zero behavior for Time mode.
5. Apply decimal points according to precision flags.
6. Generate L1 and L2 for Time mode; keep L3 off.
7. Encode each normalized segment mask through the four per-display wiring tables from `Display.md`.
8. Map matrix rows and columns into three bytes per slot.
9. Produce all five seven-byte slots directly in final SPI wire order.
10. Ensure logical matrix bits 21 through 23 are zero in dot-matrix byte 3.

### Validation

Use table-driven tests covering:

- Every digit on every numeric display.
- Every A-G and DP wiring bit.
- Minus and Error patterns.
- Every decimal position.
- Time double dots and leading zeroes.
- Blank mode.
- Matrix bits 0, 7, 8, 15, 16, and 20.
- Exact seven-byte output for representative complete slots.
- Exact 35-byte output for a representative complete frame.

### Exit criteria

Logical state deterministically produces the expected 35 SPI bytes without HAL or RTOS dependencies.

## Phase 4: SCT2xxx Multi-byte Transfer

### Tasks

1. Extend `SCT2xxx` with a method such as:

   ```cpp
   HAL_StatusTypeDef send(const uint8_t* data, uint16_t size);
   ```

2. Transmit all seven bytes in one `HAL_SPI_Transmit()` call.
3. Pulse latch once only after a successful complete transfer.
4. Preserve the currently hardware-confirmed latch sequence.
5. Keep output enable control separate from latch control.
6. Remove or retain the one-byte overload only according to actual remaining call sites.

### Validation

- Build both firmware variants.
- Use a logic analyzer to confirm seven contiguous MSB-first bytes followed by one latch pulse.
- Confirm the first wire byte reaches numeric display 4 and the last reaches numeric display 1.
- Confirm no latch pulse occurs after a failed SPI transfer.

### Exit criteria

One call shifts one complete display slot and latches it exactly once.

## Phase 5: PCB-backed Display Board and Refresh Task

### Tasks

1. Add `PcbDisplayBoard` under `User/Display`.
2. Inject or construct references to SPI3, SCT enable/latch pins, TIM2, and all five `DISPLAY_x_EN` GPIOs.
3. Own pending logical state and a 35-byte prepared frame.
4. Implement `submit()` by encoding pending state into the prepared frame.
5. Add a static high-priority refresh task using the existing `Task<StackSizeBytes>` pattern.
6. In the task, wait on a thread flag raised by the TIM2 elapsed path.
7. On each flag:
   - Disable the previous `DISPLAY_x_EN`.
   - Send the next slot's seven bytes.
   - Latch through `SCT2xxx::send()`.
   - Enable the new `DISPLAY_x_EN`.
   - Advance modulo five.
8. Start the task before enabling TIM2.
9. Call `HAL_TIM_Base_Start_IT(&htim2)` from the board's `start()` or initialization method after the task handle exists.
10. Forward TIM2 elapsed events to the board/task with a short ISR-safe operation such as `osThreadFlagsSet()`.
11. Keep SPI transfer and GPIO sequencing in task context, not timer interrupt context.
12. Do not add double buffering initially. Keep frame writes bounded and document the accepted one-frame tearing risk.

### Generated-code boundary

TIM2 base configuration, NVIC enablement, and `TIM2_IRQHandler` are already generated. Verify they remain present after regeneration. Put custom elapsed forwarding in user-owned code. If `HAL_TIM_PeriodElapsedCallback()` is already owned by the HAL time-base implementation, use an available registered callback or a small forwarding hook in an appropriate `USER CODE` block without replacing TIM1 tick behavior.

### Validation

- Build HostController and DisplayController debug targets.
- Confirm TIM2 update frequency is 250 Hz.
- Confirm each enable output is active for approximately 4 ms and the complete cycle is 20 ms.
- Confirm all columns are disabled during SPI shifting.
- Confirm there is one latch pulse and one enabled column per slot.
- Run a long-duration hardware test while Wi-Fi, USB, and logging tasks are active and inspect jitter/flicker.

### Exit criteria

A local board continuously displays a submitted static frame at 50 Hz without visible ghosting or blocking work in TIM2 ISR context.

## Phase 6: Buffer-backed Boards and Host Display

### Tasks

1. Add `BufferedDisplayBoard` with constructor-injected `I2C_HandleTypeDef&` and 7-bit slave address.
2. Validate constructor addresses are between `0x10` and `0x2A` and do not duplicate another configured remote board.
3. Store pending logical state through the common board setters.
4. Implement `submit()` by serializing command `0x01` and transmitting exactly 28 bytes to the constructor-provided address.
5. Add `Display` with one local PCB-backed board and four remote buffer-backed boards.
6. Expose board access without transferring ownership or permitting null entries.
7. Implement `Display::submit()` in stable board order: local board first, then each remote board.
8. Continue submitting later remote boards if one I2C transfer fails; retain enough result information internally for diagnostics even though setters return no status.
9. Choose and document the initial submit failure policy before completing this phase. Recommended first version:
   - No automatic retry inside `submit()`.
   - Preserve pending state for a later caller-triggered retry.
   - Log the failed address and HAL status through `DebugService` outside interrupt context.
   - Do not block local display submission because a remote board is offline.

### Variant wiring

Update `User/Src/HostController/AppVariant.cpp` to statically create:

- One local `PcbDisplayBoard`.
- Four `BufferedDisplayBoard` objects with explicitly supplied slave addresses.
- One `Display` containing those boards.

Start the local board refresh mechanism during variant initialization. Client application code performs all setters and then calls `Display::submit()`.

### Validation

- Build the HostController target.
- Capture I2C traffic and verify one 28-byte message per remote board at the configured address.
- Disconnect one remote board and confirm local submission and later remote submissions still occur.
- Verify no dynamic allocation and acceptable task/stack usage.

### Exit criteria

One Host Controller submit updates the local prepared frame and emits deterministic messages for all configured remote boards.

## Phase 7: Display Controller Address Detection and I2C Target

### Tasks

1. Add `DisplayAddress.hpp` and `.cpp` under `User/Display`.
2. Read generated pins `ADDR_0`, `ADDR_1`, and `ADDR_2` using the two-pass pull-down/pull-up procedure.
3. Assign ternary digits explicitly. Recommended mapping:
   - Ground = 0.
   - Floating = 1.
   - VCC = 2.
4. Compute `board_id = addr0 + 3 * addr1 + 9 * addr2`.
5. Compute the 7-bit address as `0x10 + board_id`.
6. Restore the address pins to a defined low-power input state after detection.
7. Add a Display Controller I2C target adapter under `User/Display`.
8. Reconfigure I2C1 from the Host-oriented generated setup to target/slave mode with the detected runtime address.
9. Enable the required I2C event/error interrupt or HAL listen mechanism in application-owned initialization.
10. Receive into a fixed 28-byte buffer; do not allocate in callbacks.
11. Validate exact length and command byte before decoding.
12. Decode into temporary logical state, copy it into the local board only on complete success, and call the local board's `submit()`.
13. Re-arm reception after success, unknown command, malformed message, and HAL error.
14. Ignore unknown commands without changing visible state.

### CubeMX and generated-code check

The common `.ioc` remains Host-oriented by design. Before implementing runtime target mode, verify which HAL initialization fields and interrupts are required for STM32G0 I2C target/listen operation. Any shared pin, clock, or NVIC capability missing from generated code must be enabled in CubeMX and regenerated; variant-specific address and listen behavior stays in `User/Display`.

### Variant wiring

Update `User/Src/DisplayController/AppVariant.cpp` to statically create and start:

- One local `PcbDisplayBoard`.
- The address detector result.
- One I2C target receiver bound to the local board.

### Validation

- Build the DisplayController target.
- Verify all 27 ternary pin combinations map to `0x10` through `0x2A` in tests or a pin-state test fixture.
- Verify the selected runtime address on an I2C analyzer.
- Send command `0x01` and confirm local display update.
- Send an unknown command, short message, and interrupted transfer; confirm the prior display remains unchanged.
- Confirm receive is re-armed after every path.

### Exit criteria

A Display Controller derives its address, receives complete command `0x01` messages, and updates its local display without partial-state exposure.

## Phase 8: Integration and Acceptance

### Tasks

1. Exercise one Host Controller with one Display Controller before adding all four remotes.
2. Add four remote board addresses and verify each board receives only its own payload.
3. Test simultaneous changes to all numeric displays and matrix rows followed by one `Display::submit()`.
4. Measure refresh behavior while Host networking, USB CDC, and debug logging are active.
5. Verify startup and recovery behavior when:
   - A remote board is absent.
   - A board resets during transfer.
   - I2C reports NACK or bus error.
   - SPI reports an error.
   - `Display::submit()` is called repeatedly.
6. Review task stack high-water marks and static RAM use.
7. Update `Display.md` if implementation choices differ from the specification.

### Required builds

```bash
"$CUBE_CMAKE" --build build/Debug-HostController
"$CUBE_CMAKE" --build build/Debug-DisplayController
```

Also build release variants when the corresponding build directories/presets exist.

### Acceptance criteria

- Local and remote displays show identical logical data for the same board state.
- Complete multiplexing cycles occur at 50 Hz or faster.
- SPI sends seven MSB-first bytes in the documented reverse device order and latches once.
- No display column is enabled while SPI data is shifting.
- I2C messages are exactly 28 bytes and unknown commands do not alter state.
- Each Display Controller responds only at its derived address.
- One offline remote board does not stop local refresh or updates to other boards.
- No heap allocation is introduced for display tasks, boards, frames, or receive buffers.
- Both firmware variants build without new warnings or errors.

## Recommended Implementation Sequence

Implement and validate one phase at a time:

1. Logical types and setters.
2. I2C serialization/deserialization.
3. PCB encoding.
4. Seven-byte SCT transfer.
5. Local refresh task and TIM2 integration.
6. Host buffer boards and `Display::submit()`.
7. Display Controller address detection and I2C target reception.
8. Multi-board integration and hardware acceptance.

Do not begin I2C target integration before the logical codec and local board path are tested. This keeps failures attributable to one layer and allows most encoding/protocol defects to be found without hardware timing in the loop.
