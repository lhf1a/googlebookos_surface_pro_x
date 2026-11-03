#!/bin/bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")" && pwd)
mountpoint -q /mnt/spx-workspace || { echo 'Workspace VHDX is not mounted' >&2; exit 1; }
export SPX_CACHE_ROOT=/mnt/spx-workspace/toolchains
export SQ2_ANDROID_SDK=$SPX_CACHE_ROOT/sq2-graphics/sdk
cd "$ROOT"
exec python3 scripts/build.py --gnu-efi "$SPX_CACHE_ROOT/gnu-efi" "$@"
