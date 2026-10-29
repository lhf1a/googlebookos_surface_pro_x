#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
K=/root/.cache/sq2-thermal/kernel
STAGE=${SQ2_SCM_TRACE_STAGE:-/root/.cache/sq2-scm-trace}
DEST="$ROOT/out/video-decode/scm-trace"
echo "307e464afa0d4c5a6d029a2e706c6a07d9a5d67322916c7716bb00973d4b1074  $K/drivers/firmware/qcom_scm.h" | sha256sum -c -
echo "4c37b875fef986233e43ecc8ea3837e7d5954d0bf73b029d8eee1860e17ee755  $K/drivers/firmware/qcom_scm-smc.c" | sha256sum -c -
echo "835050fdf779aa7e10a49ad2a0ddea07fce95ca5c122cc3a523d1c1baf007dab  $K/drivers/firmware/qcom_scm.c" | sha256sum -c -
mkdir "$STAGE"
mkdir -p "$DEST"
cp "$ROOT/probe/video/sq2_scm_trace.c" "$STAGE/"
cp "$ROOT/probe/video/sq2_pil_mem_query.c" "$STAGE/"
cp "$ROOT/probe/video/sq2-pil-region.h" "$STAGE/"
printf 'obj-m += sq2_scm_trace.o sq2_pil_mem_query.o\nccflags-y += -I$(srctree)/drivers/firmware\n' > "$STAGE/Makefile"
make -s -C "$K" O=/root/.cache/sq2-video-foundation/build ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
  M="$STAGE" KBUILD_MODPOST_WARN=1 modules > "$DEST/build.log" 2>&1
cp "$STAGE/sq2_scm_trace.ko" "$STAGE/sq2_pil_mem_query.ko" "$DEST/"
(cd "$DEST"; sha256sum sq2_scm_trace.ko sq2_pil_mem_query.ko > SHA256SUMS)
