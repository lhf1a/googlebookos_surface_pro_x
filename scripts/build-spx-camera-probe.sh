#!/bin/sh
# Build only: common camera resources, three sensor reference modules, ACPI export.
set -eu
[ "$#" -eq 3 ] || { echo "usage: $0 KERNEL_SOURCE MATCHING_BUILD NEW_OUTPUT" >&2; exit 2; }
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
src=$(realpath "$1")
build=$(realpath "$2")
out=$(realpath -m "$3")
test -f "$build/.config"
test -f "$build/Module.symvers"
[ ! -e "$out" ] || { echo "output already exists: $out" >&2; exit 2; }
mkdir -p "$out"
cp "$src/drivers/clk/qcom/camcc-sc8180x.c" "$src/drivers/i2c/busses/i2c-qcom-cci.c" "$out/"
cp "$src/drivers/media/i2c/ov5693.c" "$src/drivers/media/i2c/ov13858.c" \
   "$src/drivers/media/i2c/ov7251.c" "$src/drivers/media/v4l2-core/v4l2-cci.c"    "$src/drivers/media/v4l2-core/v4l2-async.c" "$src/drivers/media/v4l2-core/v4l2-fwnode.c"    "$src/drivers/media/v4l2-core/v4l2-subdev-priv.h" "$out/"
patch --batch --forward -d "$out" -p1 < "$root/patches/camera/0001-ov13858-of-power.patch"
patch --batch --forward -d "$out" -p1 < "$root/patches/camera/0002-ov5693-power-sequence.patch"
patch --batch --forward -d "$out" -p1 < "$root/patches/camera/0008-ov7251-continuous-clk-and-ir-strobe.patch"
cp "$root/probe/camera/spx-camera-acpi.c" "$root/probe/camera/spx-camera-acpi.h" "$root/probe/camera/spx-camera-id.c" "$root/probe/camera/spx-camera-power-read.c" \
   "$root/probe/test2/sq2-ipa-acpi.h" "$out/"
python3 - "$src/drivers/regulator/qcom_spmi-regulator.c" "$out/qcom_spmi-regulator.c" <<'PYCODE'
from pathlib import Path
import sys
s = Path(sys.argv[1]).read_text()
marker = "static const struct of_device_id qcom_spmi_regulator_match[] = {"
assert s.count(marker) == 1
s = s.replace(marker, """/* SPX camera-only table; register types are detected by the upstream driver. */
static const struct spmi_regulator_data spx_camera_regulators[] = {
 { "l1", 0x4000, "vdd_l1" },
 { "l14", 0x4d00, "vdd_l14" },
 { "l17", 0x5000, "vdd_l17" },
 { }
};
""" + marker + """
 { .compatible = "microsoft,spx-camera-spmi-regulators", .data = spx_camera_regulators },""")
Path(sys.argv[2]).write_text(s)
PYCODE
printf 'obj-m += spx-camera-power-read.o qcom_spmi-regulator.o spx-camera-id.o camcc-sc8180x.o i2c-qcom-cci.o spx-camera-acpi.o ov5693.o ov13858.o ov7251.o v4l2-cci.o v4l2-async.o v4l2-fwnode.o\nCFLAGS_v4l2-cci.o += -DCONFIG_V4L2_CCI_I2C=1\nCFLAGS_ov5693.o += -DCONFIG_V4L2_CCI_I2C=1\nccflags-y += -I%s/drivers/clk/qcom\n' "$src" > "$out/Makefile"
cc -Wall -Wextra -Werror -fsanitize=address,undefined -fno-pie -no-pie \
   "$root/tests/spx_camera_acpi_test.c" -o "$out/acpi-host-test"
"$out/acpi-host-test"
make -C "$src" O="$build" ARCH=arm64 CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}" M="$out" modules
echo "Built only; verify against the running kernel before loading. No camera stream enabled."
