#!/usr/bin/env bash
# Isolated partial adapter only. No platform match, device access or installation.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
K=${SQ2_VIDEO_KERNEL:-/root/.cache/sq2-thermal/kernel}
O=${SQ2_VIDEO_BUILD:-/root/.cache/sq2-video-foundation/build}
STAGE=${SQ2_IRIS1_STAGE:-/root/.cache/sq2-video-iris1/venus}
DEST=${SQ2_IRIS1_DEST:-"$ROOT/out/video-decode/iris1-hfi"}
mkdir -p "$DEST"
python3 "$ROOT/scripts/prepare-sq2-venus-iris1.py" --kernel "$K" --output "$STAGE" "$@"
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  "$ROOT/tests/sq2_venus_platform_test.c" -o "$STAGE/owner-test"
"$STAGE/owner-test" | tee "$DEST/owner-test.txt"
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  "$ROOT/tests/sq2_venus_lifetime_test.c" -o "$STAGE/lifetime-test"
"$STAGE/lifetime-test" | tee "$DEST/lifetime-test.txt"
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  "$ROOT/tests/sq2_venus_firmware_test.c" -o "$STAGE/firmware-test"
"$STAGE/firmware-test" | tee "$DEST/firmware-test.txt"
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  "$ROOT/tests/sq2_venus_dma_test.c" -o "$STAGE/dma-test"
"$STAGE/dma-test" | tee "$DEST/dma-test.txt"
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  "$ROOT/tests/sq2_fw_transaction_test.c" -o "$STAGE/transaction-test"
"$STAGE/transaction-test" | tee "$DEST/transaction-test.txt"
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -I "$STAGE" "$ROOT/tests/sq2_venus_iris1_test.c" -o "$STAGE/mock-test"
"$STAGE/mock-test" | tee "$DEST/mock-test.txt"
make -s -C "$K" O="$O" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
  M="$STAGE" CONFIG_VIDEO_QCOM_VENUS=m KBUILD_MODPOST_WARN=1 modules \
  > "$DEST/build.log" 2>&1
cp "$STAGE"/*.ko "$STAGE/candidate.json" "$DEST/"
(cd "$DEST" && sha256sum ./*.ko > SHA256SUMS)
echo 'COMPILE_ONLY: partial Iris1 HFI adapter; no match table, power, firmware lifetime or decode validation'
