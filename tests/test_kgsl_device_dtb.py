"""Offline synthetic DT fixtures only, never a publishable bandwidth policy."""
import copy
import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("dtgate", ROOT / "scripts/check-kgsl-device-dtb.py")
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)
GPU = GATE.GPU


def u32(*v):
    return struct.pack(">" + "I" * len(v), *v)


def fdt(nodes):
    strings, offsets, block = b"", {}, b""
    for props in nodes.values():
        for name in props:
            if name not in offsets:
                offsets[name] = len(strings)
                strings += name.encode() + b"\0"

    def node(path):
        name = path.rsplit("/", 1)[-1].encode() + b"\0"
        data = u32(1) + name + b"\0" * (-len(name) % 4)
        for key, value in nodes[path].items():
            data += u32(3, len(value), offsets[key]) + value + b"\0" * (-len(value) % 4)
        for child in nodes:
            if child != "/" and (child.rsplit("/", 1)[0] or "/") == path:
                data += node(child)
        return data + u32(2)

    block = node("/") + u32(9)
    return (u32(0xD00DFEED, 56 + len(block) + len(strings), 56,
                56 + len(block), 40, 17, 16, 0, len(strings), len(block))
            + bytes(16) + block + strings)


class DeviceDTTests(unittest.TestCase):
    def setUp(self):
        # Deliberately synthetic numbers; these are not hardware calibration.
        self.hz = [177, 256, 315, 405, 461, 500, 514, 590, 661, 700]
        self.policy = dict(ddr_table_kBps=[0, 1, 2], frequency_bindings=[
            dict(gpu_hz=f, min_index=1, default_index=1, max_index=2) for f in self.hz])
        self.base = {"/": {}, "/soc@0": {}, GPU: {
            "compatible": b"qcom,adreno-680.1\0qcom,adreno\0",
            "operating-points-v2": u32(8), "interconnect-names": b"gfx-mem\0"},
            GPU + "/opp-table": {"phandle": u32(8)},
            "/regulator": {"regulator-min-microvolt": b"\x00\x02K\x00"}}
        for freq in self.hz:
            self.base[GPU + "/opp-table/opp-" + str(freq)] = {"opp-hz": u32(0, freq)}
        self.new = copy.deepcopy(self.base)
        self.new[GPU].update({"qcom,kgsl-bus-policy": b"indexed\0", "qcom,bus-table-ddr": u32(0, 1, 2)})
        parent = GPU + "/qcom,gpu-pwrlevels"
        self.new[parent] = {}
        for freq in [0] + self.hz:
            self.new[parent + "/row-" + str(freq)] = {
                "qcom,gpu-freq": u32(freq), "qcom,bus-min": u32(1 if freq else 0),
                "qcom,bus-freq": u32(1 if freq else 0), "qcom,bus-max": u32(2 if freq else 0)}

    def result(self):
        return GATE.check(fdt(self.base), fdt(self.new), self.policy)

    def test_complete_synthetic_contract_not_hardware_acceptance(self):
        result = self.result()
        self.assertTrue(result["passed"])
        self.assertFalse(result["hardware_validated"])

    def test_three_disabled_rejected(self):
        for freq in [590, 661, 700]:
            self.new[GPU + "/opp-table/opp-" + str(freq)]["status"] = b"disabled\0"
        result = self.result()
        self.assertFalse(result["passed"])
        self.assertEqual(len(result["candidate_enabled_hz"]), 7)

    def test_incomplete_policy_rejected(self):
        self.policy["frequency_bindings"] = self.policy["frequency_bindings"][:7]
        self.assertEqual(self.result()["missing_policy_hz"], [590, 661, 700])

    def test_explicit_off_reference_row_supported(self):
        self.policy["frequency_bindings"].append(dict(gpu_hz=0, min_index=0, default_index=0, max_index=0))
        self.assertTrue(self.result()["passed"])

    def test_nonzero_off_reference_rejected(self):
        self.policy["frequency_bindings"].append(dict(gpu_hz=0, min_index=0, default_index=1, max_index=0))
        with self.assertRaises(ValueError):
            self.result()

    def test_raw_regulator_bytes_not_text_roundtrip(self):
        self.new["/regulator"]["regulator-min-microvolt"] = b"\x02K\x00"
        self.assertIn("/regulator:regulator-min-microvolt", self.result()["unrelated_changes"])

    def test_voltage_change_rejected(self):
        self.new[GPU + "/opp-table/opp-700"]["opp-level"] = u32(432)
        self.assertFalse(self.result()["passed"])

    def test_gmu_change_rejected(self):
        self.new["/soc@0/gmu"] = {"status": b"disabled\0"}
        self.assertFalse(self.result()["passed"])

    def test_peak_only_rejected(self):
        del self.new[GPU]["qcom,kgsl-bus-policy"]
        self.assertFalse(self.result()["passed"])

    def test_nonzero_average_rejected(self):
        self.base[GPU + "/opp-table/opp-177"]["opp-avg-kBps"] = u32(1)
        self.new[GPU + "/opp-table/opp-177"]["opp-avg-kBps"] = u32(1)
        self.assertFalse(self.result()["passed"])

    def test_disabled_policy_parent_rejected(self):
        self.new[GPU + "/qcom,gpu-pwrlevels"]["status"] = b"disabled\0"
        self.assertFalse(self.result()["passed"])

    def test_missing_off_rejected(self):
        del self.new[GPU + "/qcom,gpu-pwrlevels/row-0"]
        self.assertFalse(self.result()["passed"])

    def test_duplicate_policy_frequency_rejected(self):
        self.new[GPU + "/qcom,gpu-pwrlevels/row-256"]["qcom,gpu-freq"] = u32(177)
        with self.assertRaises(ValueError):
            self.result()

    def test_duplicate_opp_rejected(self):
        self.new[GPU + "/opp-table/opp-256"]["opp-hz"] = u32(0, 177)
        with self.assertRaises(ValueError):
            self.result()

    def test_unresolved_phandle_rejected(self):
        self.new[GPU]["operating-points-v2"] = u32(9)
        with self.assertRaises(ValueError):
            self.result()

    def test_reservation_change_rejected(self):
        candidate = bytearray(fdt(self.new))
        # Boot CPU is immutable too; no device writes involved.
        struct.pack_into(">I", candidate, 28, 1)
        self.assertFalse(GATE.check(fdt(self.base), candidate, self.policy)["passed"])

    def test_truncated_blob_rejected(self):
        with self.assertRaises(ValueError):
            GATE.read_fdt(fdt(self.base)[:-1])

    def test_table_maximum_rejected(self):
        self.policy["frequency_bindings"][0]["max_index"] = 10
        with self.assertRaises(ValueError):
            self.result()


if __name__ == "__main__":
    unittest.main()
