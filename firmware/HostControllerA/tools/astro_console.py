#!/usr/bin/env python3
"""CLI for talking to the HostControllerA USB CDC console.

Wraps the connect/send/read pattern from docs/Development.md so that
manual verification and ad-hoc scripting don't need to reimplement it.
Requires pyserial (python -m pip install pyserial).

Examples:
    python tools/astro_console.py list
    python tools/astro_console.py send help
    python tools/astro_console.py send "adc log on" "adc log off"
    python tools/astro_console.py shell
    python tools/astro_console.py capture --duration 20 --command "astro refresh" --output capture.txt
"""

import argparse
import sys
import threading
import time

import serial
import serial.tools.list_ports

DEFAULT_BAUD = 115200
EXCLUDED_DESCRIPTION_TOKENS = ("stlink", "st-link")


def find_console_port():
    candidates = [
        port
        for port in serial.tools.list_ports.comports()
        if not any(
            token in (port.description or "").lower()
            for token in EXCLUDED_DESCRIPTION_TOKENS
        )
    ]
    if len(candidates) == 1:
        return candidates[0].device
    if not candidates:
        return None
    raise RuntimeError(
        "multiple candidate ports found: {} (specify --port)".format(
            ", ".join(port.device for port in candidates)
        )
    )


def resolve_port(explicit_port):
    if explicit_port:
        return explicit_port
    port = find_console_port()
    if port is None:
        raise RuntimeError(
            "no candidate serial port found; connect the board or specify --port"
        )
    return port


def open_connection(port, baud):
    try:
        return serial.Serial(port, baud, timeout=0.2)
    except serial.SerialException as error:
        raise RuntimeError("could not open {}: {}".format(port, error)) from error


def send_command(connection, command):
    connection.write((command + "\r\n").encode())
    connection.flush()


def drain_for(connection, duration, on_data):
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        chunk = connection.read(256)
        if chunk:
            on_data(chunk)


def cmd_list(_args):
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("no serial ports found")
        return
    for port in ports:
        print("{}  {}".format(port.device, port.description))


def cmd_send(args):
    port = resolve_port(args.port)
    with open_connection(port, args.baud) as connection:
        print("# connected to {} @ {}".format(port, args.baud), file=sys.stderr)
        for command in args.command:
            send_command(connection, command)
            drain_for(
                connection,
                args.wait,
                lambda chunk: sys.stdout.write(chunk.decode(errors="replace")),
            )
    sys.stdout.flush()


def cmd_shell(args):
    port = resolve_port(args.port)
    with open_connection(port, args.baud) as connection:
        print(
            "# connected to {} @ {} - Ctrl+C to exit".format(port, args.baud),
            file=sys.stderr,
        )
        stop = threading.Event()

        def reader():
            while not stop.is_set():
                chunk = connection.read(256)
                if chunk:
                    sys.stdout.write(chunk.decode(errors="replace"))
                    sys.stdout.flush()

        reader_thread = threading.Thread(target=reader, daemon=True)
        reader_thread.start()
        try:
            while True:
                line = input()
                send_command(connection, line)
        except (EOFError, KeyboardInterrupt):
            pass
        finally:
            stop.set()
            reader_thread.join(timeout=1)


def cmd_capture(args):
    port = resolve_port(args.port)
    with open_connection(port, args.baud) as connection:
        print(
            "# connected to {} @ {}, capturing for {}s".format(
                port, args.baud, args.duration
            ),
            file=sys.stderr,
        )
        chunks = []
        start = time.monotonic()
        sent = args.command is None
        while time.monotonic() - start < args.duration:
            if not sent and time.monotonic() - start >= args.send_at:
                send_command(connection, args.command)
                sent = True
            chunk = connection.read(256)
            if chunk:
                chunks.append(chunk)
        data = b"".join(chunks).decode(errors="replace")

    if args.output:
        with open(args.output, "w", encoding="utf-8") as handle:
            handle.write(data)
        print("# wrote {} bytes to {}".format(len(data), args.output), file=sys.stderr)
    else:
        print(data)


def build_parser():
    parser = argparse.ArgumentParser(
        description="Connect to the HostControllerA USB CDC console"
    )
    parser.add_argument(
        "--port",
        help="serial port (default: auto-detect, excluding ST-LINK VCP)",
    )
    parser.add_argument(
        "--baud",
        type=int,
        default=DEFAULT_BAUD,
        help="baud rate (ignored by USB CDC, default {})".format(DEFAULT_BAUD),
    )

    subparsers = parser.add_subparsers(dest="action", required=True)

    subparsers.add_parser("list", help="list available serial ports").set_defaults(
        func=cmd_list
    )

    send_parser = subparsers.add_parser(
        "send", help="send one or more commands and print the response"
    )
    send_parser.add_argument("command", nargs="+", help="command line(s) to send")
    send_parser.add_argument(
        "--wait", type=float, default=2.0, help="seconds to wait after each command"
    )
    send_parser.set_defaults(func=cmd_send)

    subparsers.add_parser(
        "shell", help="interactive console: type commands, see live output"
    ).set_defaults(func=cmd_shell)

    capture_parser = subparsers.add_parser(
        "capture", help="capture device output for a fixed duration"
    )
    capture_parser.add_argument(
        "--duration", type=float, default=15.0, help="total capture duration in seconds"
    )
    capture_parser.add_argument("--command", help="optional command to send during the capture")
    capture_parser.add_argument(
        "--send-at",
        type=float,
        default=3.0,
        help="seconds into the capture to send --command",
    )
    capture_parser.add_argument(
        "--output", help="write captured output to this file instead of stdout"
    )
    capture_parser.set_defaults(func=cmd_capture)

    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        args.func(args)
    except RuntimeError as error:
        print("error: {}".format(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
