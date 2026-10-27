#!/usr/bin/env bash
# Local credential-bearing addon; never distribute its output or shadow input.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=/root/.cache/sq2-probe-build
shadow=${SQ2_SSH_SHADOW_FILE:?Set to a locally provisioned shadow file}
S=$(mktemp -d "$B/password-ssh-pack.XXXXXX")
mkdir -p "$S/bin" "$S/etc" "$ROOT/out/password-ssh"
cp "$ROOT/out/password-ssh/dropbear" "$S/bin/dropbear"
cp "$ROOT/probe/test2/start-ssh" "$S/bin/start-ssh"
cp "$shadow" "$S/etc/shadow"
chmod 600 "$S/etc/shadow"
chmod 755 "$S/bin/"*
python3 "$ROOT/scripts/cpio-list.py" "$S" > "$S.list"
"$B/kernel-android-out/usr/gen_init_cpio" "$S.list" | gzip -n > "$ROOT/out/password-ssh/addon.cpio.gz"
sha256sum "$ROOT/out/password-ssh/addon.cpio.gz"
