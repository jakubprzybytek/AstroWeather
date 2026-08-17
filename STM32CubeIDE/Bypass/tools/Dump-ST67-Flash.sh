#!/usr/bin/env bash
#
# Dump-ST67-Flash.sh
#
# Purpose:
#   Non-destructive bulk read of ST67W611M flash content through the
#   STM32G0B0 Bypass USB CDC <-> USART2 <-> ST67 ROM bootloader transport.
#   Reads real, already-flashed application code back over the bridge to
#   prove it can move firmware-sized binary data reliably (a stronger,
#   real-hardware complement to Stage D of
#   docs/ST67_Bootloader_Mode_Implementation_Plan.md). This only reads
#   flash; it never erases or writes, so it never needs --force.
#
# Usage:
#   ./Dump-ST67-Flash.sh --port <PORT> [--start <hex>] [--length <hex>]
#       [--output <path>] [--sdk-root <path>]
#
#   --port       Required. Serial device for the Bypass USB CDC port.
#   --start      Optional. Flash start address (hex, e.g. 0x0). Default 0x0.
#   --length     Optional. Bytes to read (hex, e.g. 0x180000). Default
#                0x180000 (1.5 MiB): covers boot2, the partition table, and
#                the start of the application image. This chip's flash_id
#                (ef4016) is a 4 MiB (0x400000) part, so --length can be
#                raised up to that for a full-chip dump.
#   --output     Optional. Where to save the raw dump. Defaults to
#                st67-flash-dump-<start>-<length>.bin in the current
#                directory. Not deleted automatically.
#   --sdk-root   Optional. Path to the imported x-cube-st67w61 package.
#
#   Example:
#     ./Dump-ST67-Flash.sh --port COM4 --length 0x180000
#
set -euo pipefail

die() {
  echo "Error: $*" >&2
  exit 1
}

usage() {
  sed -n '2,33p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

port=""
start="0x0"
length="0x180000"
output=""
sdk_root=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --port) port="${2:-}"; shift 2 ;;
    --start) start="${2:-}"; shift 2 ;;
    --length) length="${2:-}"; shift 2 ;;
    --output) output="${2:-}"; shift 2 ;;
    --sdk-root) sdk_root="${2:-}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) die "Unknown argument: $1" ;;
  esac
done

[[ -n "$port" ]] || die "Missing required --port <PORT>"
[[ "$start" =~ ^0x[0-9A-Fa-f]+$ ]] || die "Start must be a hex address, e.g. 0x0"
[[ "$length" =~ ^0x[0-9A-Fa-f]+$ ]] || die "Length must be a hex value, e.g. 0x180000"

start_dec=$((start))
length_dec=$((length))
[[ $length_dec -gt 0 ]] || die "Length must be greater than zero"
end_dec=$((start_dec + length_dec - 1))
end_hex="$(printf '0x%x' "$end_dec")"

if [[ -z "$output" ]]; then
  output="st67-flash-dump-${start}-${length}.bin"
fi
output_dir="$(cd "$(dirname "$output")" && pwd)"
output="$output_dir/$(basename "$output")"

check_port() {
  local port="$1"
  case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
      # COM ports are not filesystem entries on Windows; ask the .NET port list.
      if command -v powershell.exe >/dev/null 2>&1; then
        local available
        available="$(powershell.exe -NoProfile -Command '[System.IO.Ports.SerialPort]::GetPortNames()' 2>/dev/null | tr -d '\r')"
        if [[ -n "$available" ]] && ! grep -qxi "$port" <<< "$available"; then
          die "Port $port was not found. Available ports: $(tr '\n' ',' <<< "$available" | sed 's/,$//')"
        fi
      fi
      ;;
    *)
      [[ -e "$port" ]] || die "Port $port was not found."
      ;;
  esac
}
check_port "$port"

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(dirname "$script_dir")"
if [[ -z "$sdk_root" ]]; then
  sdk_root="$project_root/External/x-cube-st67w61"
fi
[[ -d "$sdk_root" ]] || die "X-CUBE-ST67 SDK root was not found: $sdk_root"
sdk_root="$(cd "$sdk_root" && pwd)"

qconn_root="$sdk_root/Projects/ST67W6X_Scripts/Binaries/QConn_Flash"
[[ -d "$qconn_root" ]] || die "QConn_Flash directory was not found: $qconn_root"

case "$(uname -s)" in
  Linux*) flash_cmd_name="QConn_Flash_Cmd-ubuntu" ;;
  MINGW*|MSYS*|CYGWIN*) flash_cmd_name="QConn_Flash_Cmd.exe" ;;
  *) die "Unsupported OS for QConn_Flash_Cmd: $(uname -s)" ;;
esac

mapfile -t flash_cmd_candidates < <(find "$qconn_root" -type f -iname "$flash_cmd_name" 2>/dev/null)
[[ ${#flash_cmd_candidates[@]} -eq 1 ]] || die "Expected exactly one $flash_cmd_name under $qconn_root, found ${#flash_cmd_candidates[@]}."
flash_cmd="${flash_cmd_candidates[0]}"

qconn_output="$output"
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    command -v cygpath >/dev/null 2>&1 || die "cygpath is required to pass the output path to QConn_Flash_Cmd"
    qconn_output="$(cygpath -w "$output")"
    ;;
esac

echo "Reading $length_dec bytes of ST67 flash from $start to $end_hex (read-only) through $port"
"$flash_cmd" --read --port "$port" --start="$start" --end="$end_hex" --file="$qconn_output"

[[ -f "$output" ]] || die "QConn_Flash_Cmd reported success but did not create the dump: $output"

actual_size=$(wc -c < "$output")
echo
echo "Saved dump to: $output"
echo "Requested $length_dec bytes, got $actual_size bytes"
[[ "$actual_size" -eq "$length_dec" ]] || echo "Warning: dump size does not match the requested length." >&2

sample_size=65536
[[ "$actual_size" -lt "$sample_size" ]] && sample_size="$actual_size"
unique_bytes=$(head -c "$sample_size" "$output" | od -An -tx1 | tr -s ' ' '\n' | sort -u | grep -c '.' || true)
echo "First $sample_size bytes contain $unique_bytes distinct byte values."
if [[ "$unique_bytes" -le 1 ]]; then
  echo "Warning: sampled region looks blank/erased (single repeated byte), not real code." >&2
else
  echo "Content looks non-blank: this is real data, not an erased region."
fi

if command -v sha256sum >/dev/null 2>&1; then
  echo "SHA-256: $(sha256sum "$output" | awk '{print $1}')"
elif command -v certutil.exe >/dev/null 2>&1; then
  certutil.exe -hashfile "$output" SHA256 | sed -n '2p'
fi

echo
echo "Read completed successfully. No flash was erased, written, or locked."
