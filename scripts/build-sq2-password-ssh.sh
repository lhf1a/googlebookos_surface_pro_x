#!/usr/bin/env bash
# Build only the server; preserve existing device host keys and client identities.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=/root/.cache/sq2-probe-build
test -f "$B/libxcrypt-4.4.38.tar.xz"
if [ ! -f "$B/password-ssh-sysroot/lib/libcrypt.a" ]; then
    tar -xf "$B/libxcrypt-4.4.38.tar.xz" -C "$B"
    cd "$B/libxcrypt-4.4.38"
    CC=aarch64-linux-gnu-gcc ./configure --host=aarch64-linux-gnu --prefix="$B/password-ssh-sysroot" --disable-shared --enable-static > configure.log
    make -j16 > build.log 2>&1
    make install > install.log
fi
mkdir -p "$B/dropbear-password-build" "$ROOT/out/password-ssh"
cd "$B/dropbear-password-build"
printf '#define DROPBEAR_SVR_PASSWORD_AUTH 1\n#define DROPBEAR_SVR_PAM_AUTH 0\n' > localoptions.h
CPPFLAGS="-I$B/password-ssh-sysroot/include" LDFLAGS="-L$B/password-ssh-sysroot/lib" CC=aarch64-linux-gnu-gcc "$B/dropbear-2026.94/configure" --host=aarch64-linux-gnu --enable-static --disable-zlib --disable-lastlog --disable-utmp --disable-wtmp --disable-utmpx --disable-wtmpx --disable-loginfunc > configure.log
make -j16 PROGRAMS=dropbear > build.log 2>&1
aarch64-linux-gnu-strip -o "$ROOT/out/password-ssh/dropbear" dropbear
