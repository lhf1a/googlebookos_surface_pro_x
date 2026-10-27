#!/usr/bin/env bash
# Refresh only the diagnostic shell scripts; preserve modules and device nodes.
# Run as root inside the build WSL distro. All destinations are regular files.
set -Eeuo pipefail
[[ $EUID -eq 0 ]] || { echo 'Run in WSL as root to preserve initramfs device nodes.'; exit 1; }
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD=${SQ2_BUILD_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/sq2-probe-build}
mkdir -p "$BUILD"
STAGE=$(mktemp -d "$BUILD/repack.XXXXXX")
gzip -dc "$ROOT/out/probe/initramfs.cpio.gz" | (cd "$STAGE" && cpio -idmu --quiet)
cp "$ROOT/probe/init" "$STAGE/init"
cp "$ROOT/probe/collect-probe" "$ROOT/probe/save-probe" "$STAGE/bin/"
chmod 755 "$STAGE/init" "$STAGE/bin/collect-probe" "$STAGE/bin/save-probe"
[[ -c "$STAGE/dev/console" && -c "$STAGE/dev/null" ]]
BACKUP=$(mktemp "$BUILD/initramfs-before.XXXXXX.gz")
cp "$ROOT/out/probe/initramfs.cpio.gz" "$BACKUP"
(cd "$STAGE" && find . -print0 | LC_ALL=C sort -z | cpio --null -o -H newc --owner=0:0 --quiet) | gzip -n -9 > "$ROOT/out/probe/initramfs.cpio.gz"
echo "Initramfs refreshed; backup: $BACKUP"
