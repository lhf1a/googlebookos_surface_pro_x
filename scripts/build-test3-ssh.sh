#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=/root/.cache/sq2-probe-build
SRC="$B/dropbear-2026.94"
echo 'e098034a843699200c8c977a991fff73159735bf795d5f72ef672c41a6b1ae81  '"$ROOT/downloads/dropbear-2026.94.tar.bz2" | sha256sum -c -
mkdir -p "$B/dropbear-build" "$ROOT/out/test2/ssh" "$ROOT/private/sq2-ssh"
cd "$B/dropbear-build"
printf '#define DROPBEAR_SVR_PASSWORD_AUTH 0\n#define DROPBEAR_SVR_PAM_AUTH 0\n' > localoptions.h
CC=aarch64-linux-gnu-gcc "$SRC/configure" --host=aarch64-linux-gnu --enable-static --disable-zlib --disable-lastlog --disable-utmp --disable-wtmp --disable-utmpx --disable-wtmpx --disable-loginfunc
make -j16 PROGRAMS='dropbear dropbearkey scp'
for f in dropbear dropbearkey scp; do
    aarch64-linux-gnu-strip -o "$ROOT/out/test2/ssh/$f" "$f"
done
key="$ROOT/private/sq2-ssh/dropbear_ed25519_host_key"
if [ ! -e "$key" ]; then qemu-aarch64 ./dropbearkey -t ed25519 -f "$key"; fi
qemu-aarch64 ./dropbearkey -y -f "$key" > "$ROOT/private/sq2-ssh/host-public.txt"
awk '/^ssh-ed25519 / {print "sq2-probe " $1 " " $2}' "$ROOT/private/sq2-ssh/host-public.txt" > "$ROOT/private/sq2-ssh/known_hosts"
test -n "${SQ2_AUTHORIZED_KEY_FILE:-}" || { echo 'Set SQ2_AUTHORIZED_KEY_FILE to your own OpenSSH public key file.' >&2; exit 1; }
test -f "$SQ2_AUTHORIZED_KEY_FILE"
cp "$SQ2_AUTHORIZED_KEY_FILE" "$ROOT/private/sq2-ssh/authorized_keys"
