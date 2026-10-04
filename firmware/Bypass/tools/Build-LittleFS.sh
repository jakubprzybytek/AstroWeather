#!/usr/bin/env bash
#
# Build-LittleFS.sh
#
# Purpose:
#   Generate the LittleFS image that Program-ST67.sh writes to the ST67's
#   file-system partition (0x378000) when used with
#   tools/astroweather_t01_flash_prog_cfg.ini.
#
#   The image holds only what the HostController firmware needs: the CA
#   certificate(s) under tools/littlefs/Certificates/lfs/. ST's stock image
#   carries 31 sample certificates and keys, and the X-CUBE driver lists the
#   whole directory (AT+FS=0,5) before every certificate upload, one line per
#   ~100 ms, which overruns its 2 s command timeout and fails every HTTPS fetch
#   (observed 2026-10-04). Three entries list in well under a second.
#
#   The certificate file must be byte-identical to the PEM compiled into the
#   HostController (User/Src/WiFi/TrustedCa.cpp, CRLF line endings); the driver
#   compares the two and rewrites the module's copy when they differ.
#
# Usage:
#   ./Build-LittleFS.sh [--sdk-root <path>]
#
#   Output: tools/littlefs/littlefs.bin (git-ignored). Same geometry as ST's
#   LittleFS/build.sh: 4096-byte blocks, 256-byte read/prog, 0x6d000 bytes.
#
set -euo pipefail

die() {
  echo "Error: $*" >&2
  exit 1
}

sdk_root=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --sdk-root) sdk_root="${2:-}"; shift 2 ;;
    -h|--help) sed -n '2,27p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "Unknown argument: $1" ;;
  esac
done

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(dirname "$script_dir")"
[[ -n "$sdk_root" ]] || sdk_root="$project_root/External/x-cube-st67w61"
[[ -d "$sdk_root" ]] || die "X-CUBE-ST67 SDK root was not found: $sdk_root"

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) mklfs="$sdk_root/Projects/ST67W6X_Scripts/LittleFS/mklfs/mklfs.exe" ;;
  Linux*) mklfs="$sdk_root/Projects/ST67W6X_Scripts/LittleFS/mklfs/mklfs-ubuntu" ;;
  *) die "Unsupported OS: $(uname -s)" ;;
esac
[[ -x "$mklfs" || -f "$mklfs" ]] || die "mklfs was not found: $mklfs"

source_dir="$script_dir/littlefs/Certificates"
output="$script_dir/littlefs/littlefs.bin"
[[ -d "$source_dir/lfs" ]] || die "Certificate directory was not found: $source_dir/lfs"
count=$(find "$source_dir/lfs" -type f | wc -l)
[[ $count -gt 0 ]] || die "No files in $source_dir/lfs"

# mklfs packs the directory given to -c, relative to the current directory,
# the way ST's build.sh does it.
cd "$script_dir/littlefs"
rm -f "$output"
"$mklfs" -c Certificates -b 4096 -p 256 -r 256 -s 0x6d000 -i ./littlefs.bin
[[ -f "$output" ]] || die "mklfs produced no image"
echo "Built $output ($(wc -c < "$output") bytes) from $count file(s):"
find "$source_dir/lfs" -type f -printf '  %f (%s bytes)\n'
