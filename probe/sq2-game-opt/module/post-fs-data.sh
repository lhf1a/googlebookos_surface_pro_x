#!/system/bin/sh
MODDIR="${0%/*}"
SRC="$MODDIR/system/vendor/etc/sq2-vulkan-policy.conf"
DST="/vendor/etc/sq2-vulkan-policy.conf"

# Ensure /vendor/etc/sq2-vulkan-policy.conf is mounted even when KernelSU has no metamodule
if [ -f "$SRC" ] && [ -f "$DST" ]; then
    if ! grep -q "type unity = qcom" "$DST" 2>/dev/null; then
        chcon --reference="$DST" "$SRC" 2>/dev/null || chcon u:object_r:vendor_configs_file:s0 "$SRC" 2>/dev/null || true
        mount -o bind "$SRC" "$DST"
    fi
fi
