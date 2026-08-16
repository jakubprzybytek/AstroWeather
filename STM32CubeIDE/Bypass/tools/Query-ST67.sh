#!/usr/bin/env bash
#
# Query-ST67.sh
#
# Purpose:
#   Non-destructive ST67W611M identification/lock-state query through the
#   STM32G0B0 Bypass USB CDC <-> USART2 <-> ST67 ROM bootloader transport
#   (Stage E of docs/ST67_Bootloader_Mode_Implementation_Plan.md). This only
#   reads the efuse/chip-info region (0x000-0x1FF); it never erases, writes,
#   or flashes anything, so it never needs a --force confirmation. Use this
#   to confirm the Bypass firmware and transport work before ever running
#   Program-ST67.sh with --force.
#
# Usage:
#   ./Query-ST67.sh --port <PORT> [--sdk-root <path>] [--chipname <name>]
#
#   --port       Required. Serial device for the Bypass USB CDC port
#                (e.g. COM4 on Windows/Git Bash, /dev/ttyACM0 on Linux).
#   --sdk-root   Optional. Path to the imported x-cube-st67w61 package.
#                Defaults to External/x-cube-st67w61 next to this script.
#   --chipname   Optional. Vendor eflash-loader chip identifier.
#                Defaults to qcc743 (matches the vendor NCP_get_chip_info
#                scripts; do not change unless ST documents a different
#                value for this module).
#
#   Example:
#     ./Query-ST67.sh --port COM4
#
set -euo pipefail

die() {
  echo "Error: $*" >&2
  exit 1
}

usage() {
  sed -n '2,29p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

port=""
sdk_root=""
chipname="qcc743"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --port) port="${2:-}"; shift 2 ;;
    --sdk-root) sdk_root="${2:-}"; shift 2 ;;
    --chipname) chipname="${2:-}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) die "Unknown argument: $1" ;;
  esac
done

[[ -n "$port" ]] || die "Missing required --port <PORT>"

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

ncp_info_root="$sdk_root/Projects/ST67W6X_Scripts/Binaries/NCP_info"
[[ -d "$ncp_info_root" ]] || die "NCP_info directory was not found: $ncp_info_root"

case "$(uname -s)" in
  Linux*) eflash_name="QConn_Eflash-ubuntu" ;;
  MINGW*|MSYS*|CYGWIN*) eflash_name="QConn_Eflash.exe" ;;
  *) die "Unsupported OS for QConn_Eflash: $(uname -s)" ;;
esac

mapfile -t eflash_candidates < <(find "$ncp_info_root" -maxdepth 1 -type f -iname "$eflash_name" 2>/dev/null)
[[ ${#eflash_candidates[@]} -eq 1 ]] || die "Expected exactly one $eflash_name under $ncp_info_root, found ${#eflash_candidates[@]}."
eflash="${eflash_candidates[0]}"

decoder=""
decoder_kind=""
if [[ -f "$ncp_info_root/read_chip_info.exe" && "$(uname -s)" =~ ^(MINGW|MSYS|CYGWIN) ]]; then
  decoder="$ncp_info_root/read_chip_info.exe"
  decoder_kind="exe"
elif [[ -f "$ncp_info_root/read_chip_info.py" ]] && command -v python3 >/dev/null 2>&1; then
  decoder="$ncp_info_root/read_chip_info.py"
  decoder_kind="py"
fi

dump_file=""
cleanup() {
  if [[ -n "$dump_file" && -f "$dump_file" ]]; then
    rm -f -- "$dump_file" || echo "Warning: could not remove $dump_file" >&2
  fi
}
trap cleanup EXIT

dump_file="$(mktemp)"

echo "Reading ST67 efuse/chip-info region (0x000-0x1FF, read-only) through $port"
"$eflash" -r --efuse --chipname="$chipname" -p "$port" --start=0x0 --end=0x1ff --file="$dump_file"

echo
if [[ "$decoder_kind" == "exe" ]]; then
  "$decoder" "$dump_file"
elif [[ "$decoder_kind" == "py" ]]; then
  python3 "$decoder" "$dump_file"
else
  echo "No chip-info decoder available on this system; raw dump kept at: $dump_file" >&2
  echo "Install python3 or run this on Windows to decode it with read_chip_info.exe/.py." >&2
  trap - EXIT
  exit 0
fi

echo
echo "Query completed successfully. No flash was erased, written, or locked."
