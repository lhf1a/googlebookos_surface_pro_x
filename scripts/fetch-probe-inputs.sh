#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"
mkdir -p downloads upstream/aarch64-kernel-configs/full
fetch() {
    local url=$1 destination=$2
    if [[ ! -f "$destination" ]]; then
        [[ ! -L "$destination" && ! -L "$destination.part" ]] || { echo 'Refusing symlink output'; exit 1; }
        curl -fL --retry 3 --connect-timeout 30 -o "$destination.part" "$url"
        mv "$destination.part" "$destination"
    fi
}
fetch https://codeload.github.com/linux-surface/kernel/tar.gz/b86f786594dc9e53c57fcfa2aa58ada9541506bb downloads/surface-kernel-b86f786.tar.gz
fetch https://github.com/linux-surface/grub-image-aarch64/releases/download/fedora-39-1/bootaa64.efi downloads/bootaa64.efi
fetch https://busybox.net/downloads/busybox-1.36.1.tar.bz2 downloads/busybox-1.36.1.tar.bz2
fetch https://raw.githubusercontent.com/qzed/aarch64-kernel-configs/7281316889197641763c29242c0120de70c3db11/full/spx-5.19.0-small.config upstream/aarch64-kernel-configs/full/spx-5.19.0-small.config
sha256sum -c configs/probe-inputs.sha256
