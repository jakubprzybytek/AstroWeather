# Talking to the Device over COM Port

## Purpose

Notes on how to observe device log/telemetry output and send interactive commands to a running HostController board over its USB CDC virtual COM port (e.g. `COM5` on Windows). See [USB_CDC_Debug_Service.md](USB_CDC_Debug_Service.md) for the underlying protocol implemented by `DebugService` and `ConsoleService`.

## Connection Settings

- Port: whatever the OS assigns to the board's USB CDC device (check Device Manager on Windows, or `ls /dev/tty*` on Linux/macOS).
- Baud rate: irrelevant — this is a USB CDC (virtual COM port), not a real UART, so any value (e.g. `115200`) works.
- Line ending: send `\n` (or `\r\n`); the device treats `\r` as ignorable and `\n` as the line terminator.

## Option A: VS Code Serial Monitor Extension

`eclipse-cdt.serial-monitor` ("Web and Desktop Serial Monitor") is a GUI option for manual, interactive sessions:

1. Open the Serial Monitor view (Command Palette → "Serial Monitor: Start Monitoring").
2. Select the port and any baud rate, connect.
3. Type a line and press Enter to send; incoming log/echo lines appear live in the panel.

Good for manual poking around. Not scriptable, so not ideal for automated capture.

## Option B: Scripted Capture with Python (pyserial)

For reproducible, automatable captures (e.g. "run for N seconds, send a command, collect output"), use `pyserial`:

```bash
python -m pip install --quiet pyserial
```

Example one-off script pattern (adjust `PORT`, `DURATION`, `SEND_AT`, and the command sent as needed):

```python
import serial
import time

PORT = "COM5"
BAUD = 115200
DURATION = 15   # total seconds to capture
SEND_AT = 3     # send the command this many seconds in

ser = serial.Serial(PORT, BAUD, timeout=0.2)
start = time.monotonic()
sent = False
buf = b""

while time.monotonic() - start < DURATION:
    elapsed = time.monotonic() - start
    if not sent and elapsed >= SEND_AT:
        ser.write(b"help\r\n")
        ser.flush()
        sent = True
    chunk = ser.read(256)
    if chunk:
        buf += chunk

ser.close()
print(buf.decode(errors="replace"))
```

Notes:
- `timeout=0.2` on the `Serial` object makes `read()` non-blocking-ish (returns after up to 0.2s with whatever is available), so the loop can poll for the send deadline while still capturing output.
- Only one process can hold the port open at a time — close any open Serial Monitor panel session before running a script against the same port, and vice versa.
- Treat such scripts as throwaway/one-off (write them to a temp location, run, then delete) rather than committing them to the repo, unless the team wants a permanent capture tool.

## Command Protocol Reference

The device's line-based interface is documented in detail in [USB_CDC_Debug_Service.md](USB_CDC_Debug_Service.md). Key points:

- Every line sent produces an echo response: `[days:hours:minutes:seconds]: received text`.
- `ConsoleService` interprets specific commands (discoverable via `help`), including at least:
  - `help` — list commands
  - `status` — system status
  - `display set <index> <value> <precision>`
  - `display time <index> <HH:MM>`
  - `display blank <index>`
  - `adc on` / `adc off` — toggle current-sense readout logging
- Unsolicited log lines (`[INFO]`, `[WARN]`, `[ERR]`, `[DEBUG]`) and periodic `[STATS]` / `[MEM]` / `[STACK]` telemetry (every 5s of inactivity) arrive interleaved with command responses — a capture window should be long enough to separate the two.
- Command set may evolve; re-run `help` to get the current list rather than trusting this document to stay exhaustive.
