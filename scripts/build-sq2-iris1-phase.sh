#!/usr/bin/env bash
# Consumer only: never creates/removes the VIDEOCC overlay/provider.
# Bind via driver_override after the existing zero-power probe has completed.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
STAGE=${SQ2_IDENTIFY_STAGE:-/root/.cache/sq2-iris1-phase}
DEST="$ROOT/out/video-decode/iris1-phase"
case "${1:-}" in
  ''|--firmware) ;;
  *) echo 'Expected optional --firmware' >&2; exit 2 ;;
esac
mkdir "$STAGE"
mkdir -p "$DEST"
if [ "${1:-}" = --firmware ]; then
  echo '5dd1d028daafe8006400ca50bc6b7ec54a933521f0f6f1c672445e92a9c3d30b  /root/.cache/sq2-thermal/kernel/drivers/soc/qcom/mdt_loader.c' | sha256sum -c -
  cp "$ROOT/probe/video/sq2_iris1_identify.c" "$STAGE/identify.c"
  cp "$ROOT/probe/video/"{sq2-venus-fw.c,sq2-venus-fw.h,sq2-fw-transaction.h,sq2_video_fw_layout.h} "$STAGE/"
  printf 'obj-m += sq2_iris1_phase.o\nsq2_iris1_phase-objs := identify.o sq2-venus-fw.o\nccflags-y += -DSQ2_IDENTIFY_EXISTING -DSQ2_IDENTIFY_FIRMWARE\n' > "$STAGE/Makefile"
  gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    "$ROOT/tests/sq2_fw_transaction_test.c" -o "$STAGE/transaction-test"
  "$STAGE/transaction-test" | tee "$DEST/transaction-test.txt"
else
  cp "$ROOT/probe/video/sq2_iris1_identify.c" "$STAGE/sq2_iris1_phase.c"
  printf 'obj-m += sq2_iris1_phase.o\nccflags-y += -DSQ2_IDENTIFY_EXISTING\n' > "$STAGE/Makefile"
fi
make -s -C /root/.cache/sq2-thermal/kernel \
  O=/root/.cache/sq2-video-foundation/build ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
  M="$STAGE" KBUILD_MODPOST_WARN=1 modules > "$DEST/build.log" 2>&1
cp "$STAGE/sq2_iris1_phase.ko" "$DEST/"
(cd "$DEST" && sha256sum sq2_iris1_phase.ko > SHA256SUMS)
echo 'BUILD_ONLY: default zero-power/no-MMIO; stages require a separate bounded collector'
