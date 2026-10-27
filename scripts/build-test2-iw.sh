#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=/root/.cache/sq2-probe-build/tools-test2
mkdir -p "$B" "$ROOT/out/test2"
cd "$ROOT/downloads"
echo '3f2db22ad41c675242b98ae3942dbf3112548c60a42ff739210f2de4e98e4894  iw-6.9.tar.xz' | sha256sum -c -
echo '2a56e1edefa3e68a7c00879496736fdbf62fc94ed3232c0baba127ecfa76874d  libnl-3.11.0.tar.gz' | sha256sum -c -
cd "$B"
tar -xf "$ROOT/downloads/libnl-3.11.0.tar.gz"
tar -xf "$ROOT/downloads/iw-6.9.tar.xz"
cd libnl-3.11.0
./configure --host=aarch64-linux-gnu --prefix="$B/install" --disable-shared --enable-static --disable-cli --disable-pthreads
make -j16
make install
cd "$B/iw-6.9"
PKG_CONFIG_LIBDIR="$B/install/lib/pkgconfig" make -j16 CC=aarch64-linux-gnu-gcc LDFLAGS=-static
aarch64-linux-gnu-strip iw
cp iw "$ROOT/out/test2/iw"
qemu-aarch64 "$ROOT/out/test2/iw" --version
