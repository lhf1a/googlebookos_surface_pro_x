"""Host execution tests for the early-init raw FDT fingerprint helper."""
import importlib.util
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("dtfixture", ROOT / "tests/test_kgsl_device_dtb.py")
FIX = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FIX)


@unittest.skipUnless(shutil.which("gcc"), "requires host C compiler")
class FDTContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.directory = Path(cls.tmp.name)
        cls.tool = cls.directory / "sq2-fdt-contract"
        subprocess.run(["gcc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                        str(ROOT / "probe/test2/sq2-fdt-contract.c"), "-o", str(cls.tool)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_blob(self, blob):
        p = self.directory / "input.dtb"
        p.write_bytes(blob)
        return subprocess.run([str(self.tool), str(p)], capture_output=True, timeout=10)

    def fixture(self):
        return {"/": {}, "/chosen": {"bootargs": b"old\0"},
                "/gpu": {"opp-hz": FIX.u32(0, 700000000)},
                "/regulator": {"voltage": b"\x00\x02K\x00"}}

    def test_explicit_fixups_only_are_ignored(self):
        a = self.fixture()
        before = self.run_blob(FIX.fdt(a))
        a["/chosen"].update({"bootargs": b"different\0", "kaslr-seed": bytes(8)})
        a["/"]["microsoft,el2-pas-trace"] = bytes(4)
        after = self.run_blob(FIX.fdt(a))
        self.assertEqual((before.returncode, after.returncode), (0, 0))
        self.assertEqual(before.stdout, after.stdout)

    def test_unrelated_chosen_property_not_ignored(self):
        a = self.fixture()
        before = self.run_blob(FIX.fdt(a)).stdout
        a["/chosen"]["new-property"] = bytes(4)
        self.assertNotEqual(before, self.run_blob(FIX.fdt(a)).stdout)

    def test_regulator_raw_bytes_detected(self):
        a = self.fixture()
        before = self.run_blob(FIX.fdt(a)).stdout
        a["/regulator"]["voltage"] = b"\x02K\x00"
        self.assertNotEqual(before, self.run_blob(FIX.fdt(a)).stdout)

    def test_disabled_opp_detected(self):
        a = self.fixture()
        before = self.run_blob(FIX.fdt(a)).stdout
        a["/gpu"]["status"] = b"disabled\0"
        self.assertNotEqual(before, self.run_blob(FIX.fdt(a)).stdout)

    def test_changed_phy_detected(self):
        a = self.fixture()
        before = self.run_blob(FIX.fdt(a)).stdout
        a["/gpu"]["phys"] = FIX.u32(123)
        self.assertNotEqual(before, self.run_blob(FIX.fdt(a)).stdout)

    def test_node_inventory_detected(self):
        a = self.fixture()
        before = self.run_blob(FIX.fdt(a)).stdout
        a["/new-node"] = {}
        self.assertNotEqual(before, self.run_blob(FIX.fdt(a)).stdout)

    def test_truncated_rejected(self):
        self.assertEqual(self.run_blob(FIX.fdt(self.fixture())[:-1]).returncode, 2)

    def test_bad_magic_rejected(self):
        blob = bytearray(FIX.fdt(self.fixture()))
        blob[0] = 0
        self.assertEqual(self.run_blob(blob).returncode, 2)

    def test_out_of_bounds_structure_rejected(self):
        blob = bytearray(FIX.fdt(self.fixture()))
        struct.pack_into(">I", blob, 8, len(blob) + 8)
        self.assertEqual(self.run_blob(blob).returncode, 2)

    def test_duplicate_dynamic_property_rejected(self):
        a = self.fixture()
        a["/chosen"]["fakename"] = b"not-the-same\0"
        blob = FIX.fdt(a).replace(b"fakename\0", b"bootargs\0")
        r = self.run_blob(blob)
        self.assertEqual(r.returncode, 2)
        self.assertIn(b"duplicate", r.stderr)

    def test_duplicate_static_property_rejected(self):
        a = self.fixture()
        a["/gpu"]["aaaaaa"] = b"one"
        a["/gpu"]["bbbbbb"] = b"two"
        r = self.run_blob(FIX.fdt(a).replace(b"bbbbbb\0", b"aaaaaa\0"))
        self.assertEqual(r.returncode, 2)

    def test_too_deep_rejected(self):
        a = {"/": {}}
        path = ""
        for _ in range(257):
            path += "/n"
            a[path] = {}
        self.assertEqual(self.run_blob(FIX.fdt(a)).returncode, 2)

    def test_long_path_expansion_has_bounded_record_budget(self):
        a, path = {"/": {}}, ""
        for _ in range(150):
            path += "/" + "a" * 50
            a[path] = {}
        for i in range(2500):
            a[path + "/leaf" + str(i)] = {}
        r = self.run_blob(FIX.fdt(a))
        self.assertEqual(r.returncode, 2)
        self.assertIn(b"record budget exceeded", r.stderr)


if __name__ == "__main__":
    unittest.main()
