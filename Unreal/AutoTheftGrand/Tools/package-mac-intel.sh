#!/usr/bin/env bash
# Build a Shipping package for Intel (x86_64) Macs. Run on macOS with UE 5.8 and Xcode installed.
# Override UE_ROOT if Unreal is installed somewhere other than the Epic Games default.
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "Intel Mac packaging must run on macOS with Apple's toolchain; this host is $(uname -s)." >&2
  exit 1
fi

UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
UAT="$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh"
PROJECT="$HERE/AutoTheftGrand.uproject"
ARCHIVE="$HERE/Build/MacIntel"

if [[ ! -x "$UAT" ]]; then
  echo "Unreal Automation Tool not found at: $UAT" >&2
  echo "Set UE_ROOT to the root of the installed Unreal Engine 5.8." >&2
  exit 1
fi

"$UAT" BuildCookRun \
  -project="$PROJECT" \
  -noP4 \
  -platform=Mac \
  -clientconfig=Shipping \
  -build -cook -stage -pak -archive \
  -archivedirectory="$ARCHIVE" \
  -specifiedarchitecture=x86_64 \
  -utf8output \
  "$@"

echo "Intel Mac package archived under: $ARCHIVE"
