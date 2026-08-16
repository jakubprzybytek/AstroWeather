# ST67 Bootloader Mode Implementation Plan

## 1. Objective

Implement and validate `ST67_MODE_BOOTLOADER` in the standalone STM32G0B0
Bypass firmware so that a PC can program the attached ST67W611M through the
following path:

```text
QConn_Flash_Cmd
    <-> native USB CDC
    <-> STM32G0B0 Bypass firmware
    <-> USART2 at 2,000,000 baud
    <-> ST67W611M ROM bootloader
```

The STM32 must only:

1. apply the verified ST67 bootloader strap and reset sequence;
2. provide a binary-transparent, bidirectional transport; and
3. expose enough status and error information to diagnose transport failures.

The STM32 must not implement, parse, acknowledge, or otherwise interpret the
proprietary QConn/ST67 programming protocol. Erase, program, verify, partition,
and efuse operations remain the responsibility of the vendor-provided
`QConn_Flash_Cmd` host utility.

## 2. Source of Truth

Use the X-CUBE-ST67W61 package imported at:

```text
External/x-cube-st67w61
```

The authoritative reference implementation is:

```text
External/x-cube-st67w61/Projects/NUCLEO-U575ZI-Q/Utilities/NCP/NCP_Loader
```

The relevant reference behavior is in `Core/Src/main.c`:

- `NCP_FLASH_MODE` selects the host binary that enters ST67 flash mode.
- `uart_bypass(1)` applies the ST67 bootloader entry sequence.
- `uart_pass_through()` provides an unmodified full-duplex byte path.

The host-side reference workflow and programming assets are in:

```text
External/x-cube-st67w61/Projects/ST67W6X_Scripts/Binaries
```

In particular:

- `NCP_update_mission_profile_t01.bat`
- `NCP_update_mission_profile_t02.bat`
- `NCP_update_mfg.bat`
- `QConn_Flash/QConn_Flash_Cmd.exe`
- `NCP_Binaries/*.ini`
- `NCP_Binaries/*.bin`

Do not infer protocol details from traffic captures when the corresponding
vendor configuration or tool already defines the operation.

## 3. Confirmed Bootloader Entry Sequence

The NUCLEO-U575ZI-Q loader applies this flash-mode sequence:

```text
1. Drive BOOT high.
2. Drive CHIP_EN low.
3. Wait 100 ms.
4. Drive CHIP_EN high.
5. Wait 100 ms.
6. Drive BOOT low.
7. Start or continue transparent UART forwarding.
```

For the AstroWeather Bypass hardware, keep `ST67_CS` high throughout UART
bootloader operation. This preserves the already established UART-mode state
and prevents the SPI interface from being selected.

The planned sequence is therefore:

```text
ST67_CS      = 1
ST67_BOOT    = 1
ST67_CHIP_EN = 0
wait 100 ms
ST67_CHIP_EN = 1
wait 100 ms
ST67_BOOT    = 0
```

Important timing properties:

- `BOOT` must be stable before the rising edge of `CHIP_EN`.
- `BOOT` must remain high for the complete 100 ms post-enable interval used by
  the reference implementation.
- `CS` must already be high before `CHIP_EN` rises.
- UART data must not be transmitted while `CHIP_EN` is low or during the
  post-enable delay.
- The final low state of `BOOT` must not cause another reset; it only releases
  the strap after it has been sampled.

These values should initially match the reference exactly. They may only be
reduced after successful electrical measurements and repeated programming
tests demonstrate that a shorter interval is supported.

## 4. Existing Bypass Baseline

The current project already provides:

- STM32G0B0 firmware generated from `Bypass.ioc`;
- USART2 on PA2/PA3 at 2,000,000 baud, 8-N-1;
- native USB CDC;
- binary-transparent USB-to-UART and UART-to-USB forwarding;
- an interrupt-driven UART receive path;
- ring buffers and overflow counters;
- correct USB transmit-buffer ownership through the transmit-complete callback;
- `ST67_MODE_MANUFACTURE`; and
- a successful standalone Debug build.

The implementation must preserve these properties. Bootloader support is an
extension of mode entry and host tooling, not a replacement bridge.

## 5. Scope

### 5.1 In scope

- Implement the ST67 bootloader GPIO sequence.
- Make bootloader mode selectable at build time.
- Produce clearly distinguishable manufacture and bootloader firmware
  artifacts or presets.
- Ensure CDC open/reopen behavior resets the ST67 into the selected mode.
- Ensure the bridge is ready before the ST67 can emit bootloader traffic.
- Add a Bypass-specific host programming script.
- Add explicit dependency and image validation to the host script.
- Validate mode entry electrically.
- Validate real erase/program/verify operations with the vendor QConn tool.
- Document recovery and failure diagnosis.

### 5.2 Out of scope

- Reimplementing `QConn_Flash_Cmd`.
- Reverse engineering the ST67 programming protocol.
- Embedding ST67 mission images in STM32 flash.
- Implementing an autonomous field updater in the STM32.
- Programming ST67 efuses by default.
- Dynamic switching between manufacture and bootloader modes through bytes in
  the transparent data stream.
- Porting the U575 GPIO bit-banging loop.
- Loading the U575 `Bootloader.bin` on the STM32G0B0.

## 6. Design Decisions

### 6.1 Continue using hardware USART2

The official loader bit-bangs GPIO because it bridges the NUCLEO ST-LINK VCP
pins directly to the ST67. The Bypass board has a native USB device and a
dedicated USART2 connection, so hardware UART remains the correct
implementation.

Benefits:

- deterministic framing at 2 Mbaud;
- interrupt-driven receive buffering;
- lower CPU use;
- explicit overrun detection; and
- no timing dependency on compiler optimization or interrupt masking.

### 6.2 Use compile-time mode selection

Do not introduce in-band commands into the CDC byte stream. Any such command
could collide with binary bootloader data.

Use a compile definition:

```text
ST67_DEFAULT_MODE=ST67_DEFAULT_MODE_BOOTLOADER
```

Provide separate CMake configure/build presets so the intended behavior is
visible before flashing.

Recommended preset and artifact names:

```text
Debug-Manufacture
Release-Manufacture
Debug-Bootloader
Release-Bootloader
```

The resulting ELF files should be placed in distinct build directories. If BIN
generation is added using an existing project toolchain facility, name the
outputs unambiguously:

```text
Bypass_Manufacture.bin
Bypass_Bootloader.bin
```

Do not rely on a single stale `Bypass.elf` whose selected mode cannot be
identified from its path.

### 6.3 Keep mode-specific timing in `st67_mode.c`

The bridge must not contain strap levels or reset timing. `bridge.c` should
request re-entry into the configured mode, while `st67_mode.c` owns:

- GPIO levels;
- reset pulse duration;
- post-enable duration; and
- validation of supported enum values.

### 6.4 Reset on CDC DTR rising edge

The current bridge requests an ST67 reset when the host first asserts DTR. Keep
this behavior because it ensures that the bootloader starts while the
programming client is connected.

Requirements:

- reset only on a false-to-true DTR transition;
- never call `HAL_Delay` in USB interrupt context;
- defer reset execution to `Bridge_Process`;
- do not reset on line-coding changes;
- do not reset repeatedly while DTR remains asserted; and
- verify the QConn tool's actual DTR behavior.

If QConn does not assert DTR, the startup reset performed from `main()` must
still leave the ST67 in bootloader mode. If QConn toggles DTR more than once
during one programming operation, add a state/cooldown policy based on observed
behavior rather than guessing.

## 7. Firmware Implementation

### Phase 1: Make the mode definitions build-safe

File:

```text
User/Inc/st67_mode.h
```

Tasks:

1. Retain the `St67Mode` enum.
2. Retain distinct numeric preprocessor values for manufacture and bootloader
   defaults because the preprocessor cannot compare C enum identifiers.
3. Remove the current unconditional compile-time rejection of bootloader mode.
4. Add a compile-time validity check that rejects every value other than:
   - `ST67_DEFAULT_MODE_MANUFACTURE`; or
   - `ST67_DEFAULT_MODE_BOOTLOADER`.
5. Keep the function declaration typed as:

   ```c
   void ST67_EnterMode(St67Mode mode);
   ```

6. Optionally add a small mode query only if it is needed for diagnostics. Do
   not add unused APIs.

Expected result:

- both supported mode definitions compile;
- an invalid numeric definition fails compilation with a clear error.

### Phase 2: Implement bootloader GPIO sequencing

File:

```text
User/Src/st67_mode.c
```

Tasks:

1. Replace generic 20 ms timing names with mode-specific constants:

   ```c
   #define ST67_MANUFACTURE_ENABLE_PULSE_MS 20u
   #define ST67_MANUFACTURE_STARTUP_DELAY_MS 20u
   #define ST67_BOOTLOADER_ENABLE_PULSE_MS 100u
   #define ST67_BOOTLOADER_STRAP_HOLD_MS 100u
   ```

2. Set safe common levels first:

   ```text
   CHIP_EN = low
   CS      = high
   ```

3. For `ST67_MODE_MANUFACTURE`:
   - set `BOOT` low;
   - wait the existing manufacture enable pulse;
   - raise `CHIP_EN`;
   - wait the manufacture startup delay.

4. For `ST67_MODE_BOOTLOADER`:
   - set `BOOT` high;
   - wait 100 ms with `CHIP_EN` low;
   - raise `CHIP_EN`;
   - wait 100 ms;
   - set `BOOT` low.

5. Use an explicit `switch` over `St67Mode`.
6. Route unsupported values to `Error_Handler`; never silently select
   manufacture mode.
7. Do not manipulate USART2, USB, ring indices, or IRQ state in this function.
8. Do not wait on `ST67_RDY` unless authoritative documentation establishes
   its bootloader semantics. It may be sampled for diagnostics later.

Implementation ordering must prevent strap glitches. The initial CubeMX GPIO
output values must remain:

```text
CHIP_EN = 0
CS      = 1
BOOT    = 0
```

Bootloader mode may transition `BOOT` from low to high only while `CHIP_EN` is
already low.

### Phase 3: Verify startup order

File:

```text
Core/Src/main.c
```

Required order:

```text
HAL_Init
clock initialization
GPIO initialization with CHIP_EN low
USART2 initialization
Bridge_Init
USB device initialization
optional unused peripheral initialization
ST67_EnterMode(selected mode)
foreground Bridge_Process loop
```

Review whether `MX_SPI1_Init` is necessary. It is not required by the
bootloader path, but removing it is a separate cleanup unless it creates an
electrical conflict. The critical requirement is that `ST67_CS` stays under
GPIO control and high while using UART.

Before calling `ST67_EnterMode`, clear stale transport state if any code path
could already have received bytes. At cold startup this should not be needed
because the ST67 remains disabled until mode entry.

### Phase 4: Make reset requests transport-safe

Files:

```text
User/Src/bridge.c
USB_Device/App/usbd_cdc_if.c
```

Review and test the existing deferred-reset path:

1. `CDC_SET_CONTROL_LINE_STATE` extracts DTR.
2. `Bridge_OnHostConnectionChanged` detects a rising edge.
3. The callback sets `st67_reset_pending`.
4. `Bridge_Process` calls `ST67_EnterMode` outside interrupt context.

Before resetting the ST67 for a new host session:

- discard unread UART-to-USB bytes from the previous ST67 session;
- discard queued USB-to-UART bytes from the previous host session;
- clear UART overrun/error flags;
- drain any stale UART RX data register contents; and
- preserve cumulative diagnostic counters unless a deliberate session-counter
  API is introduced.

This prevents bytes from a previous manufacture or bootloader session from
being delivered after reset.

Do not disable USB interrupts for the 200 ms bootloader entry delay. USB must
remain enumerated and responsive while the foreground loop is delayed. Confirm
that the watchdog, if later enabled, permits this blocking interval.

### Phase 5: Strengthen transport diagnostics

Current overflow counters are file-local and cannot be inspected without a
debugger. For initial bring-up, debugger inspection is acceptable. Before
declaring the tool reliable, choose one non-invasive diagnostic mechanism:

- expose counters through debugger symbols; or
- expose a separate diagnostic build/interface that is not active during
  binary programming.

Do not insert log text into the programming CDC stream.

At minimum, retain and inspect:

- USB-to-UART ring overflow count;
- UART-to-USB ring overflow count;
- UART hardware overrun count; and
- USB transmit failure/busy retry behavior.

Consider adding a counter for explicit session resets and for UART framing,
noise, and parity errors if those flags can be reported without destabilizing
the receive ISR.

## 8. CMake and Artifact Plan

Files:

```text
CMakeLists.txt
CMakePresets.json
```

### 8.1 CMake definition

Replace the single hard-coded manufacture definition with a cache variable:

```cmake
set(ST67_DEFAULT_MODE "MANUFACTURE" CACHE STRING
    "ST67 startup mode: MANUFACTURE or BOOTLOADER")
set_property(CACHE ST67_DEFAULT_MODE PROPERTY STRINGS
    MANUFACTURE BOOTLOADER)

if(ST67_DEFAULT_MODE STREQUAL "MANUFACTURE")
    set(ST67_DEFAULT_MODE_DEFINE ST67_DEFAULT_MODE_MANUFACTURE)
elseif(ST67_DEFAULT_MODE STREQUAL "BOOTLOADER")
    set(ST67_DEFAULT_MODE_DEFINE ST67_DEFAULT_MODE_BOOTLOADER)
else()
    message(FATAL_ERROR "Unsupported ST67_DEFAULT_MODE: ${ST67_DEFAULT_MODE}")
endif()

target_compile_definitions(Bypass PRIVATE
    ST67_DEFAULT_MODE=${ST67_DEFAULT_MODE_DEFINE}
)
```

Use the project target variable rather than duplicating `Bypass` if that is the
existing local convention.

### 8.2 Presets

Add configure presets with separate binary directories:

```text
build/Debug-Manufacture
build/Release-Manufacture
build/Debug-Bootloader
build/Release-Bootloader
```

Each preset must set both:

- `CMAKE_BUILD_TYPE`; and
- `ST67_DEFAULT_MODE`.

Add matching build presets. Keep preset names symmetric so automation can map
configure and build stages without special cases.

### 8.3 Build verification matrix

Build all four presets:

| Preset | Expected mode | Purpose |
|---|---|---|
| Debug-Manufacture | Manufacture | Regression and debugger bring-up |
| Release-Manufacture | Manufacture | Existing utility behavior |
| Debug-Bootloader | Bootloader | Electrical and counter inspection |
| Release-Bootloader | Bootloader | Actual programming workflow |

Also configure once with an invalid mode and confirm CMake fails before
compilation.

## 9. Host Programming Script

Create a Bypass-specific PowerShell script rather than modifying the vendor
script in place.

Recommended location:

```text
tools/Program-ST67.ps1
```

If the project intentionally keeps all utilities under `docs` or another
existing folder, follow that convention, but do not place executable scripts
inside the ignored vendor SDK tree.

### 9.1 Responsibilities

The script must:

1. Locate the imported X-CUBE package from a parameter or a documented default.
2. Locate `QConn_Flash_Cmd.exe`.
3. Locate the selected flash configuration and every referenced image.
4. Identify the Bypass native USB CDC COM port.
5. Reject ambiguous detection when more than one candidate device exists.
6. Confirm the user selected the intended profile and version.
7. Warn clearly if the operation may lock an unlocked NCP.
8. Invoke QConn with absolute, quoted paths.
9. propagate the QConn exit code;
10. report failure as failure and avoid success-shaped output;
11. avoid invoking STM32CubeProgrammer unless a separate explicit option is
    provided to flash the STM32 Bypass firmware; and
12. never write the U575 `Bootloader.bin` to the STM32G0B0.

### 9.2 Suggested parameters

```text
-Port COMx
-Profile MissionT01 | MissionT02 | Manufacturing
-Version 2.0.106
-SdkRoot <path>
-ConfigPath <optional explicit INI>
-Force
```

`-Port` should be mandatory initially. Automatic VID/PID or serial-number
detection can be added after the project has a distinct, stable USB identity.

### 9.3 Configuration generation

The vendor scripts generate a temporary INI by replacing the versioned
firmware filename. Reuse that behavior without modifying the source template.

Requirements:

- create the generated INI in a temporary, explicitly known location;
- preserve ASCII encoding if required by QConn;
- verify that exactly one expected `filedir` line was replaced;
- resolve all referenced files before launching QConn;
- remove only the generated temporary file in a `finally` block;
- do not delete or alter vendor templates; and
- make cleanup failure visible without hiding the programming result.

### 9.4 Initial supported operation

Start with one low-risk, known configuration, preferably the mission profile
already required by the application. Do not expose efuse customization as a
generic user parameter during initial bring-up.

The reference invocation shape is:

```text
QConn_Flash_Cmd.exe
    --port COMx
    --config <generated-config.ini>
    --efuse=<vendor-efusedata.bin>
```

Preserve the vendor argument format exactly. Do not assume that omitting
`--efuse` is harmless without vendor documentation.

### 9.5 Retry policy

The vendor script retries QConn once. The Bypass script may do the same, but a
retry must first re-enter bootloader mode.

Because QConn cannot directly command a GPIO reset, one of these verified
methods is required:

1. close and reopen the CDC port if that produces a DTR rising edge and a
   deterministic bootloader reset;
2. reset the STM32 Bypass firmware through an explicit supported mechanism; or
3. instruct the user to power-cycle/reset before retry.

Do not immediately rerun QConn against an ST67 left in an unknown partial
state.

## 10. USB Device Identification

The current descriptor uses the generic product string:

```text
STM32 Virtual ComPort
```

Before relying on automatic COM-port discovery:

1. give the Bypass firmware a distinct product string, such as
   `AstroWeather ST67 Bypass`;
2. ensure a stable serial-number descriptor is available per device; and
3. document the VID/PID ownership constraints.

Changing VID/PID is not required to implement bootloader mode and must not be
done without an assigned identity. Product and serial strings are sufficient
for initial local discovery if Windows exposes them consistently.

Until identification is proven, require an explicit COM port.

## 11. Detailed Validation Plan

Validation must progress in stages. Do not begin destructive or locking
operations before transport and boot entry are proven.

### Stage A: Static review

Verify:

- the bootloader sequence matches the official loader;
- `CS` is high before `CHIP_EN` rises;
- no code transmits while `CHIP_EN` is low;
- bootloader mode cannot silently fall back to manufacture mode;
- `HAL_Delay` runs only in the foreground;
- no logging is injected into CDC data;
- bootloader and manufacture build outputs cannot overwrite one another; and
- host tooling cannot select the U575 host image for the STM32G0B0.

### Stage B: Build validation

1. Configure and build all four presets.
2. Confirm no warnings are introduced in changed files.
3. Inspect linker memory usage.
4. Confirm the expected mode macro in each compile command.
5. Confirm an invalid mode fails configuration or compilation.
6. Rebuild from a clean build directory to exclude stale definitions.

### Stage C: GPIO electrical validation

Use an oscilloscope or logic analyzer on:

- `ST67_CHIP_EN`;
- `ST67_BOOT`;
- `ST67_CS`; and
- optionally USART2 TX/RX.

For bootloader mode, verify:

1. `CHIP_EN` starts low.
2. `CS` is high before the reset release.
3. `BOOT` rises while `CHIP_EN` is low.
4. at least 100 ms elapses before `CHIP_EN` rises;
5. `BOOT` remains high for at least 100 ms after `CHIP_EN` rises; and
6. `BOOT` then returns low without a `CHIP_EN` glitch.

Repeat for:

- power-on;
- STM32 reset;
- USB cable insertion;
- first CDC open;
- CDC close/reopen; and
- QConn startup.

For manufacture mode, verify the existing sequence remains unchanged.

### Stage D: Binary-transparent transport validation

Before using QConn:

1. connect a controlled UART peer or test fixture in place of the ST67;
2. send payloads in both directions containing:

   ```text
   00 01 0a 0d 1b 7f 80 fe ff
   ```

3. verify length and every byte exactly;
4. test packet sizes around:
   - USB maximum packet boundaries;
   - `USB_TX_BUFFER_SIZE`;
   - both ring sizes; and
5. run sustained full-rate traffic long enough to expose buffer pressure.

Acceptance:

- no transformed, inserted, or missing bytes;
- zero UART hardware overruns;
- zero ring overflows during the supported connected-host workload; and
- no USB TX buffer corruption.

### Stage E: Non-destructive bootloader detection

Run the least destructive vendor operation that confirms bootloader
communication, such as the package's chip-information workflow, if supported
for the target's lock state.

Verify:

- QConn opens the native CDC port;
- the ST67 responds after mode entry;
- no manual timing race is required;
- opening the port does not trigger repeated resets; and
- the command exits successfully multiple times.

Record:

- QConn version;
- ST67 identity/revision;
- lock state;
- exact Bypass firmware build;
- boot-entry waveform;
- operation duration; and
- all bridge counters.

### Stage F: Controlled programming

Use a known-compatible signed vendor image and configuration.

Before proceeding:

- back up all available identity/version information;
- confirm whether the operation locks an unlocked device;
- confirm stable board power;
- disable sleep/USB power-saving behavior on the PC if it can interrupt the
  connection;
- use a direct USB connection rather than an unverified hub; and
- ensure no other application has the COM port open.

Execute:

1. enter bootloader mode;
2. erase/program using QConn;
3. require QConn verification success;
4. capture the complete tool exit status/output;
5. reset the ST67 into the intended post-program mode;
6. query the resulting firmware version; and
7. exercise a basic functional command.

Repeat programming enough times to establish reliability, including at least:

- cold power-on;
- STM32 reset before programming; and
- CDC close/reopen before programming.

### Stage G: Failure and recovery testing

Where safe and recoverable, test:

- wrong COM port;
- COM port already open;
- missing QConn executable;
- missing image;
- malformed generated INI;
- unsupported profile/version;
- QConn nonzero exit;
- CDC disconnect before programming begins;
- CDC disconnect during a non-destructive operation; and
- retry after a failed connection.

Do not intentionally interrupt a flash write until vendor recovery behavior is
understood.

Every failure must:

- produce a nonzero script exit code;
- identify the failed stage;
- retain useful QConn output;
- avoid reporting success;
- clean up only generated temporary files; and
- state the required recovery action.

## 12. Acceptance Criteria

`ST67_MODE_BOOTLOADER` is complete only when all of the following are true:

### Firmware

- manufacture and bootloader presets build independently;
- invalid mode configuration fails clearly;
- bootloader mode applies the reference `BOOT`/`CHIP_EN` timing;
- `CS` remains high for UART bootloader operation;
- USB stays enumerated during deferred ST67 reset;
- the bridge remains binary-transparent;
- no bridge overflow or UART overrun occurs during normal programming; and
- manufacture mode still works.

### Host tooling

- the script requires or reliably discovers exactly one Bypass COM port;
- every required vendor tool/configuration/image is checked before reset or
  programming;
- the script does not flash U575 firmware to the STM32G0B0;
- QConn exit status is propagated;
- temporary files are cleaned safely; and
- retry behavior includes deterministic ST67 bootloader re-entry.

### Hardware outcome

- a non-destructive bootloader query succeeds repeatedly;
- a known signed ST67 image programs and verifies successfully;
- the ST67 exits programming mode and boots the expected image;
- firmware identity/version confirms the intended image;
- repeated programming does not require undocumented manual timing; and
- captured evidence identifies the tested hardware, firmware, QConn version,
  and image set.

## 13. Implementation Order

Use this sequence to minimize risk:

1. Add CMake mode validation and distinct presets.
2. Implement the bootloader GPIO sequence.
3. Build all mode/configuration combinations.
4. Verify waveforms without running QConn.
5. Verify binary-transparent transport at sustained 2 Mbaud.
6. Verify DTR/open behavior and stale-buffer cleanup.
7. Create the Bypass-specific host script.
8. Run a non-destructive chip-information operation.
9. Program and verify one known signed image.
10. Run failure/recovery tests.
11. Update the general Bypass guide with the now-verified bootloader details.

Do not combine the first real bootloader entry, first transport stress test,
and first destructive flash operation into one experiment.

## 14. Documentation Updates After Validation

After successful hardware validation, update
`ST67_Bypass_Bootloader_Project_Guide.md`:

- replace the statement that bootloader straps are unknown;
- document the verified `BOOT=1`, `CS=1`, 100 ms/100 ms sequence;
- document the tested QConn version;
- document the supported ST67 firmware profiles and versions;
- document the Bypass-specific programming command;
- document expected USB identity and COM-port selection;
- document recovery steps; and
- reconcile the guide's STM32G0B1 references with the actual STM32G0B0 target.

Keep measured results distinct from assumptions. Include dates and hardware
revisions for electrical measurements because future PCB or module revisions
may change the applicable evidence.

## 15. Risks and Mitigations

| Risk | Consequence | Mitigation |
|---|---|---|
| Incorrect boot strap timing | ST67 remains in mission/manufacture mode | Match official 100 ms/100 ms sequence and verify waveform |
| Generic COM-port discovery | Wrong device is programmed or operation fails | Require explicit `-Port` until stable device identity is implemented |
| DTR causes repeated resets | QConn handshake/programming fails | Detect only rising edges and characterize QConn control-line behavior |
| Stale ring-buffer data after reset | Protocol corruption | Flush queued session data before deferred reset |
| USB cannot drain 2 Mbaud stream | Overflow and programming failure | Stress test, monitor counters, tune buffers only from evidence |
| Host sleeps or USB disconnects | Interrupted programming | Stable power/direct USB and clear preflight instructions |
| Wrong profile/image combination | Unbootable or incompatible ST67 image | Validate profile, version, config, and referenced files before QConn |
| Efuse/lock operation is irreversible | Device becomes permanently locked | Preserve vendor warning and never generalize efuse parameters by default |
| Vendor SDK is ignored by Git | Another checkout lacks tools/images | Document SDK acquisition/version and do not claim the repository is self-contained |
| U575 script is reused unchanged | STM32G0 firmware is overwritten incorrectly | Use a dedicated script that never selects U575 host binaries |

## 16. Deliverables

The implementation is expected to produce:

1. completed `ST67_MODE_BOOTLOADER` logic;
2. manufacture and bootloader CMake presets;
3. independently identifiable build artifacts;
4. a Bypass-specific ST67 programming script;
5. updated user documentation;
6. captured boot-entry waveforms;
7. a recorded successful QConn chip query;
8. a recorded successful erase/program/verify cycle; and
9. a validation record containing tool, image, board, module, and firmware
   versions.
