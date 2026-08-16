#!/usr/bin/env bash
#
# Program-ST67.sh
#
# Purpose:
#   Bypass-specific wrapper around the vendor QConn_Flash_Cmd tool used to
#   program the ST67W611M NCP module through the STM32G0B0 Bypass firmware's
#   USB CDC <-> USART2 transparent transport, while the ST67 is held in its
#   ROM bootloader (see docs/ST67_Bootloader_Mode_Implementation_Plan.md).
#
#   It resolves the vendor QConn_Flash_Cmd executable, the selected flash
#   configuration (.ini) and every image it references, generates a temporary
#   ASCII configuration pointing at absolute paths, invokes QConn, propagates
#   its exact exit code, and always removes the generated temporary file.
#
#   It never invokes STM32CubeProgrammer and never selects a NUCLEO host
#   image; only the Bypass STM32G0B0 CDC port and ST67 NCP images are used.
#
# Usage:
#   ./Program-ST67.sh --port <PORT> --profile <MissionT01|MissionT02|Manufacturing> \
#       [--version <x.y.z>] [--sdk-root <path>] [--config-path <path>] [--force]
#
#   --port         Required. Serial device for the Bypass USB CDC port
#                  (e.g. COM7 on Windows/Git Bash, /dev/ttyACM0 on Linux).
#   --profile      Required. MissionT01, MissionT02, or Manufacturing.
#   --version      Optional. Firmware version (e.g. 2.0.106). Defaults to the
#                  newest version found in NCP_Binaries.
#   --sdk-root     Optional. Path to the imported x-cube-st67w61 package.
#                  Defaults to External/x-cube-st67w61 next to this script.
#   --config-path  Optional. Explicit flash configuration .ini to use instead
#                  of the default vendor template for the selected profile.
#   --force        Required to actually run QConn. Without it, the script
#                  performs every safety check, prints the lock-risk warning,
#                  and exits without programming.
#
#   Example:
#     ./Program-ST67.sh --port /dev/ttyACM0 --profile MissionT01 --force
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
profile=""
version=""
sdk_root=""
config_path=""
force=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --port) port="${2:-}"; shift 2 ;;
    --profile) profile="${2:-}"; shift 2 ;;
    --version) version="${2:-}"; shift 2 ;;
    --sdk-root) sdk_root="${2:-}"; shift 2 ;;
    --config-path) config_path="${2:-}"; shift 2 ;;
    --force) force=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "Unknown argument: $1" ;;
  esac
done

[[ -n "$port" ]] || die "Missing required --port <PORT>"
[[ -n "$profile" ]] || die "Missing required --profile <MissionT01|MissionT02|Manufacturing>"

declare -A PROFILE_PREFIX=(
  [MissionT01]="st67w611m_mission_t01"
  [MissionT02]="st67w611m_mission_t02"
  [Manufacturing]="st67w611m_mfg"
)
declare -A PROFILE_CONFIG=(
  [MissionT01]="mission_t01_flash_prog_cfg.ini"
  [MissionT02]="mission_t02_flash_prog_cfg.ini"
  [Manufacturing]="mfg_flash_prog_cfg.ini"
)
[[ -n "${PROFILE_PREFIX[$profile]+_}" ]] || die "Profile must be one of: MissionT01, MissionT02, Manufacturing"
prefix="${PROFILE_PREFIX[$profile]}"

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

case "$(uname -s)" in
  Linux*) qconn_name="QConn_Flash_Cmd-ubuntu" ;;
  MINGW*|MSYS*|CYGWIN*) qconn_name="QConn_Flash_Cmd.exe" ;;
  *) die "Unsupported OS for QConn_Flash_Cmd: $(uname -s)" ;;
esac

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(dirname "$script_dir")"
if [[ -z "$sdk_root" ]]; then
  sdk_root="$project_root/External/x-cube-st67w61"
fi
[[ -d "$sdk_root" ]] || die "X-CUBE-ST67 SDK root was not found: $sdk_root"
sdk_root="$(cd "$sdk_root" && pwd)"

binaries_root="$sdk_root/Projects/ST67W6X_Scripts/Binaries"
ncp_binary_root="$binaries_root/NCP_Binaries"
qconn_root="$binaries_root/QConn_Flash"
[[ -d "$binaries_root" ]] || die "X-CUBE-ST67 Binaries directory was not found: $binaries_root"

mapfile -t qconn_candidates < <(find "$qconn_root" -type f -iname "$qconn_name" 2>/dev/null)
[[ ${#qconn_candidates[@]} -eq 1 ]] || die "Expected exactly one $qconn_name under $qconn_root, found ${#qconn_candidates[@]}."
qconn="${qconn_candidates[0]}"

if [[ -z "$config_path" ]]; then
  template_path="$ncp_binary_root/${PROFILE_CONFIG[$profile]}"
else
  template_path="$(realpath -m "$config_path")"
fi
[[ -f "$template_path" ]] || die "Flash configuration was not found: $template_path"
template_dir="$(cd "$(dirname "$template_path")" && pwd)"

if [[ -z "$version" ]]; then
  mapfile -t version_files < <(find "$ncp_binary_root" -maxdepth 1 -type f \
    -regextype posix-extended -regex ".*/${prefix}_v[0-9]+\.[0-9]+\.[0-9]+\.bin" 2>/dev/null)
  [[ ${#version_files[@]} -gt 0 ]] || die "No versioned $profile firmware image was found in $ncp_binary_root"
  version="$(for f in "${version_files[@]}"; do
    base="$(basename "$f" .bin)"
    echo "${base#${prefix}_v}"
  done | sort -V | tail -n1)"
fi
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "Version must use the form major.minor.patch, for example 2.0.106."

selected_image="$ncp_binary_root/${prefix}_v${version}.bin"
[[ -f "$selected_image" ]] || die "Selected $profile image was not found: $selected_image"

efuse_image="$ncp_binary_root/efusedata.bin"
[[ -f "$efuse_image" ]] || die "Required efuse image was not found: $efuse_image"

if [[ $force -ne 1 ]]; then
  echo "Warning: this operation may lock an unlocked ST67 device. Use --force only after confirming the profile and image." >&2
  die "Programming was not started. Re-run with --force after reviewing the warning."
fi

firmware_pattern="^\\./${prefix}_v(xxx|[0-9]+\\.[0-9]+\\.[0-9]+)\\.bin\$"
firmware_matches=0
resolved_lines=()
while IFS= read -r raw_line || [[ -n "$raw_line" ]]; do
  line="${raw_line%$'\r'}"
  if [[ $line =~ ^[[:space:]]*filedir[[:space:]]*=[[:space:]]*([^;#]+)$ ]]; then
    configured_path="${BASH_REMATCH[1]}"
    configured_path="${configured_path%"${configured_path##*[![:space:]]}"}"
    if [[ $configured_path =~ $firmware_pattern ]]; then
      firmware_matches=$((firmware_matches + 1))
      resolved_lines+=("filedir = $selected_image")
    else
      if [[ "$configured_path" = /* ]]; then
        reference_path="$configured_path"
      else
        reference_path="$template_dir/$configured_path"
      fi
      resolved_reference="$(realpath -m "$reference_path")"
      [[ -f "$resolved_reference" ]] || die "Configuration references a missing image: $resolved_reference"
      resolved_lines+=("filedir = $resolved_reference")
    fi
  else
    resolved_lines+=("$line")
  fi
done < "$template_path"
[[ $firmware_matches -eq 1 ]] || die "Expected exactly one $profile firmware filedir entry in $template_path, found $firmware_matches."

generated_config=""
cleanup() {
  if [[ -n "$generated_config" && -f "$generated_config" ]]; then
    rm -f -- "$generated_config" || echo "Warning: could not remove $generated_config" >&2
  fi
}
trap cleanup EXIT

generated_config="$(mktemp)"
mv -- "$generated_config" "$generated_config.ini"
generated_config="$generated_config.ini"
printf '%s\n' "${resolved_lines[@]}" > "$generated_config"

echo "Programming $profile version $version through $port"
echo "Using QConn: $qconn"
result=0
"$qconn" --port "$port" --config "$generated_config" "--efuse=$efuse_image" || result=$?
if [[ $result -ne 0 ]]; then
  echo "Error: QConn_Flash_Cmd failed with exit code $result. Re-enter bootloader mode before any retry." >&2
  exit "$result"
fi
echo "QConn programming completed successfully."
