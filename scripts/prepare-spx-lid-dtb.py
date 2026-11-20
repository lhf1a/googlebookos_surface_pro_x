#!/usr/bin/env python3
"""Prepare optional Surface Pro X hardware Hall lid switch (TLMM GPIO 121) in an offline DT.

Adds a gpio-keys-lid node for the board Hall sensor on TLMM GPIO 121
(GPIO_ACTIVE_LOW, EV_SW / SW_LID, wakeup-source, EV_ACT_DEASSERTED).

Conflict guard:
  scripts/patch-sq2-lid.py patches surface_aggregator_tabletsw.c (SQ2_LID_SWITCH_V1)
  to emit SW_LID from the SSAM cover state. Enabling gpio-keys-lid at the same time
  creates two independent SW_LID input devices. Therefore this script defaults to
  status = "disabled" unless --enable is explicitly passed, and refuses --enable if
  --kernel-source contains SQ2_LID_SWITCH_V1 (unless --allow-ssam-conflict is set).

Validation boundary:
  TLMM GPIO 121 Hall lid switch was reported upstream on SQ2 (surface-pro-x-sq2-linux);
  this helper is host/DT unit-tested and NOT yet hardware-validated on Hybrid Android 17
  alongside patch-sq2-lid.py / patch-spx-unknown-lid.py.
"""
import argparse
import importlib.util
from pathlib import Path
import struct
import libfdt

spec = importlib.util.spec_from_file_location("dt", Path(__file__).with_name("spx-dt.py"))
dt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dt)

TLMM_LID_GPIO = 121
GPIO_ACTIVE_LOW = 1
EV_SW = 5
SW_LID = 0
EV_ACT_DEASSERTED = 2
SSAM_LID_MARKER = "SQ2_LID_SWITCH_V1"


def cells(*values):
    return struct.pack(">" + "I" * len(values), *values)


def strings(*values):
    return b"".join(v.encode() + b"\0" for v in values)


def has_ssam_lid_patch(kernel_source):
    path = Path(kernel_source)
    if path.is_dir():
        path = path / "drivers/platform/surface/surface_aggregator_tabletsw.c"
    if not path.is_file():
        return False
    return SSAM_LID_MARKER in path.read_text(errors="replace")


def _claims_gpio121(before, tlmm_path):
    prefix = tlmm_path + "/"
    for (path, prop), value in before.items():
        if prop == "pins" and path.startswith(prefix):
            if b"gpio121" in value.split(b"\0"):
                return path
    return None


def _existing_sw_lid_node(before):
    gpio_keys_parents = {
        path for (path, prop), value in before.items()
        if prop == "compatible" and b"gpio-keys" in value.split(b"\0")
    }
    for (path, prop), value in before.items():
        if prop != "linux,code" or value != cells(SW_LID):
            continue
        parent = path.rsplit("/", 1)[0] or "/"
        if parent in gpio_keys_parents and before.get((path, "linux,input-type")) == cells(EV_SW):
            return path
    return None


def prepare(data, *, enable=False, kernel_source=None, allow_ssam_conflict=False):
    if enable and kernel_source is not None and not allow_ssam_conflict:
        if has_ssam_lid_patch(kernel_source):
            raise ValueError(
                "SSAM SW_LID patch (SQ2_LID_SWITCH_V1 from patch-sq2-lid.py) is active in "
                "kernel source; enabling gpio-keys-lid would register a duplicate SW_LID emitter"
            )

    fdt = libfdt.Fdt(data)
    before = dt.properties(fdt)
    boards = set(before.get(("/", "compatible"), b"").split(b"\0"))
    if not {b"microsoft,surface-pro-x", b"qcom,sc8180x"} <= boards:
        raise ValueError("Surface Pro X / SC8180X required")

    existing_lid = _existing_sw_lid_node(before)
    if existing_lid is not None:
        raise ValueError(f"DT already defines a gpio-keys SW_LID switch at {existing_lid}")

    tlmm_paths = [
        path for (path, prop), value in before.items()
        if prop == "compatible" and b"qcom,sc8180x-tlmm" in value.split(b"\0")
    ]
    if len(tlmm_paths) != 1:
        raise ValueError("expected one qcom,sc8180x-tlmm node")
    tlmm_path = tlmm_paths[0]

    pin_conflict = _claims_gpio121(before, tlmm_path)
    if pin_conflict is not None:
        raise ValueError(f"TLMM gpio121 is already claimed by {pin_conflict}")

    fdt.resize(len(data) + 4096)
    maximum = max(
        (int.from_bytes(v, "big") for (_, n), v in before.items()
         if n in ("phandle", "linux,phandle") and len(v) == 4),
        default=0,
    )

    def ref(path):
        nonlocal maximum
        offset = fdt.path_offset(path)
        value = fdt.get_phandle(offset)
        if not value:
            maximum += 1
            if maximum >= 0xFFFFFFFF:
                raise ValueError("no unused phandle")
            value = maximum
            fdt.setprop_u32(offset, "phandle", value)
        return value

    def add(path, props):
        if fdt.path_offset(path, quiet=(libfdt.NOTFOUND,)) >= 0:
            raise ValueError(f"node already exists: {path}")
        parent, name = path.rsplit("/", 1)
        fdt.add_subnode(fdt.path_offset(parent or "/"), name)
        node_offset = fdt.path_offset(path)
        for key, value in props.items():
            fdt.setprop(node_offset, key, value)
        return path

    tlmm_phandle = ref(tlmm_path)
    pin_node = add(
        f"{tlmm_path}/lid-default-state",
        {
            "pins": strings("gpio121"),
            "function": strings("gpio"),
            "bias-disable": b"",
            "input-enable": b"",
        },
    )
    pin_phandle = ref(pin_node)

    lid_node = add(
        "/gpio-keys-lid",
        {
            "compatible": strings("gpio-keys"),
            "label": strings("Lid"),
            "pinctrl-names": strings("default"),
            "pinctrl-0": cells(pin_phandle),
            "status": strings("okay" if enable else "disabled"),
        },
    )
    add(
        f"{lid_node}/switch-lid",
        {
            "label": strings("Lid"),
            "gpios": cells(tlmm_phandle, TLMM_LID_GPIO, GPIO_ACTIVE_LOW),
            "linux,input-type": cells(EV_SW),
            "linux,code": cells(SW_LID),
            "wakeup-source": b"",
            "wakeup-event-action": cells(EV_ACT_DEASSERTED),
        },
    )

    fdt.pack()
    after = dt.properties(fdt)
    assert all(after[k] == v for k, v in before.items()), "existing property changed"
    original = libfdt.Fdt(data)
    assert [fdt.get_mem_rsv(i) for i in range(fdt.num_mem_rsv())] == [
        original.get_mem_rsv(i) for i in range(original.num_mem_rsv())
    ]
    return bytes(fdt.as_bytearray())


def emit_dts(*, enable=False):
    status = "okay" if enable else "disabled"
    return f"""/dts-v1/;
/plugin/;

&{{/}} {{
	gpio-keys-lid {{
		compatible = "gpio-keys";
		label = "Lid";
		pinctrl-names = "default";
		pinctrl-0 = <&lid_default>;
		status = "{status}";

		switch-lid {{
			label = "Lid";
			gpios = <&tlmm {TLMM_LID_GPIO} {GPIO_ACTIVE_LOW}>;
			linux,input-type = <{EV_SW}>;
			linux,code = <{SW_LID}>;
			wakeup-source;
			wakeup-event-action = <{EV_ACT_DEASSERTED}>;
		}};
	}};
}};

&tlmm {{
	lid_default: lid-default-state {{
		pins = "gpio121";
		function = "gpio";
		bias-disable;
		input-enable;
	}};
}};
"""


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--enable",
        action="store_true",
        help='Set status = "okay" (default is "disabled" to avoid duplicate SW_LID with patch-sq2-lid.py)',
    )
    parser.add_argument(
        "--kernel-source",
        type=Path,
        default=None,
        help="Optional kernel source root or tabletsw.c path to guard against SQ2_LID_SWITCH_V1 conflict",
    )
    parser.add_argument(
        "--allow-ssam-conflict",
        action="store_true",
        help="Explicitly override the SQ2_LID_SWITCH_V1 duplicate SW_LID conflict guard",
    )
    args = parser.parse_args()
    result = prepare(
        args.input.read_bytes(),
        enable=args.enable,
        kernel_source=args.kernel_source,
        allow_ssam_conflict=args.allow_ssam_conflict,
    )
    with args.output.open("xb") as output:
        output.write(result)
