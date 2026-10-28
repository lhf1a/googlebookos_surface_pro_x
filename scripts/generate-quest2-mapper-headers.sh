#!/usr/bin/env bash
set -Eeuo pipefail
WORK=${SQ2_QUEST2_WORK:-/root/.cache/sq2-quest2-hidl}
export LD_LIBRARY_PATH="$WORK${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
roots=(-r "android.hardware:$WORK/hal/android/hardware" -r "android.hidl:$WORK/hal/android/hidl" -r "vendor.qti.hardware:$WORK/hal/vendor/qti/hardware")
packages=(android.hidl.base@1.0 android.hidl.manager@1.0 android.hidl.manager@1.1 android.hidl.manager@1.2
  android.hardware.graphics.common@1.0 android.hardware.graphics.common@1.1 android.hardware.graphics.common@1.2
  android.hardware.graphics.mapper@2.0 android.hardware.graphics.mapper@2.1 android.hardware.graphics.mapper@3.0 android.hardware.graphics.mapper@4.0
  vendor.qti.hardware.display.mapperextensions@1.0 vendor.qti.hardware.display.mapperextensions@1.1 vendor.qti.hardware.display.mapper@4.0)
mkdir -p "$WORK/generated" "$WORK/templates"
for package in "${packages[@]}"; do
  "$WORK/hidl-gen" -o "$WORK/generated" -L c++-headers "${roots[@]}" "$package"
done
for package in vendor.qti.hardware.display.mapper@4.0 vendor.qti.hardware.display.mapperextensions@1.1; do
  "$WORK/hidl-gen" -o "$WORK/templates" -L c++-impl "${roots[@]}" "$package"
done
echo QTI_MAPPER_HEADERS_GENERATED
