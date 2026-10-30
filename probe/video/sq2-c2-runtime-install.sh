#!/system/bin/sh
# post-fs-data only. Never mount or format userdata, never overwrite a version.
set -eu
source=/vendor/etc/sq2-codec2
parent=/data/vendor/sq2-codec2
target=$parent/v1
test "$(cat /sys/module/suspend/parameters/screen_only)" = Y
grep -q ' /data ext4 ' /proc/mounts
(cd "$source"; sha256sum -c runtime.sha256)
test ! -L "$parent"
mkdir -p "$parent"
chmod 700 "$parent"
if [ -e "$target" ] || [ -L "$target" ]; then
    test -d "$target" && test ! -L "$target"
    # Compare against the immutable manifest, not a data-side replacement.
    (cd "$target"; sha256sum -c "$source/runtime-files.sha256")
    cmp "$target/SHA256SUMS" "$source/runtime-files.sha256"
else
    test ! -e "$parent/v1.new" && test ! -L "$parent/v1.new"
    mkdir "$parent/v1.new"
    tar -xf "$source/runtime.tar" -C "$parent/v1.new"
    (cd "$parent/v1.new"; sha256sum -c "$source/runtime-files.sha256")
    cmp "$parent/v1.new/SHA256SUMS" "$source/runtime-files.sha256"
    mv "$parent/v1.new" "$target"
fi
echo SQ2_RUNTIME_V1_VERIFIED
