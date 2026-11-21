import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
import libfdt

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("liddt", ROOT / "scripts/prepare-spx-lid-dtb.py")
liddt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(liddt)


class LidDtbTests(unittest.TestCase):
    def fixture(self, board="microsoft,surface-pro-x", extra_root="", extra_tlmm="", with_tlmm=True):
        tlmm = f"""
          pinctrl@3100000 {{
            compatible = "qcom,sc8180x-tlmm";
            reg = <0 0x3100000 0 0xc00000>;
            gpio-controller;
            #gpio-cells = <2>;
            {extra_tlmm}
          }};
        """ if with_tlmm else ""
        text = f"""/dts-v1/;
        /memreserve/ 0x99000000 0x1400000;
        / {{
          compatible = "{board}", "qcom,sc8180x";
          #address-cells = <2>; #size-cells = <2>;
          unrelated {{ phandle = <420>; keep = <9>; }};
          {extra_root}
          soc@0 {{
            #address-cells = <2>; #size-cells = <2>;
            {tlmm}
          }};
        }};"""
        return subprocess.check_output(["dtc", "-q", "-I", "dts", "-O", "dtb"], input=text.encode())

    def test_default_disabled_hall_lid_and_property_preservation(self):
        before = self.fixture()
        after = liddt.prepare(before)
        f_before = libfdt.Fdt(before)
        f_after = libfdt.Fdt(after)
        a = liddt.dt.properties(f_before)
        b = liddt.dt.properties(f_after)
        self.assertTrue(all(b[k] == v for k, v in a.items()))

        self.assertEqual(b["/gpio-keys-lid", "compatible"], b"gpio-keys\0")
        self.assertEqual(b["/gpio-keys-lid", "label"], b"Lid\0")
        self.assertEqual(b["/gpio-keys-lid", "status"], b"disabled\0")
        self.assertEqual(b["/gpio-keys-lid", "pinctrl-names"], b"default\0")

        tlmm_phandle = int.from_bytes(b["/soc@0/pinctrl@3100000", "phandle"], "big")
        pin_phandle = int.from_bytes(b["/soc@0/pinctrl@3100000/lid-default-state", "phandle"], "big")
        self.assertGreater(tlmm_phandle, 420)
        self.assertGreater(pin_phandle, tlmm_phandle)
        self.assertEqual(b["/gpio-keys-lid", "pinctrl-0"], liddt.cells(pin_phandle))

        self.assertEqual(b["/soc@0/pinctrl@3100000/lid-default-state", "pins"], b"gpio121\0")
        self.assertEqual(b["/soc@0/pinctrl@3100000/lid-default-state", "function"], b"gpio\0")
        self.assertEqual(b["/soc@0/pinctrl@3100000/lid-default-state", "bias-disable"], b"")
        self.assertEqual(b["/soc@0/pinctrl@3100000/lid-default-state", "input-enable"], b"")

        sw = "/gpio-keys-lid/switch-lid"
        self.assertEqual(b[sw, "label"], b"Lid\0")
        self.assertEqual(b[sw, "gpios"], liddt.cells(tlmm_phandle, 121, 1))
        self.assertEqual(b[sw, "linux,input-type"], liddt.cells(5))
        self.assertEqual(b[sw, "linux,code"], liddt.cells(0))
        self.assertEqual(b[sw, "linux,can-disable"], b"")
        self.assertEqual(b[sw, "debounce-interval"], liddt.cells(20))
        self.assertEqual(b[sw, "wakeup-source"], b"")
        self.assertEqual(b[sw, "wakeup-event-action"], liddt.cells(2))
        self.assertFalse(liddt.has_hall_lid_node(before))
        self.assertTrue(liddt.has_hall_lid_node(after))

        # Verify dtc decompiles the generated DTB cleanly.
        dts = subprocess.check_output(["dtc", "-q", "-I", "dtb", "-O", "dts"], input=after).decode()
        self.assertIn("gpio-keys-lid", dts)
        self.assertIn("lid-default-state", dts)
        self.assertIn("linux,can-disable", dts)
        self.assertIn("debounce-interval", dts)

    def test_active_high_and_custom_debounce_validation(self):
        before = self.fixture()
        after = liddt.prepare(before, enable=True, active_low=False, debounce_ms=50)
        b = liddt.dt.properties(libfdt.Fdt(after))
        tlmm_phandle = int.from_bytes(b["/soc@0/pinctrl@3100000", "phandle"], "big")
        sw = "/gpio-keys-lid/switch-lid"
        self.assertEqual(b[sw, "gpios"], liddt.cells(tlmm_phandle, 121, 0))
        self.assertEqual(b[sw, "wakeup-event-action"], liddt.cells(1))
        self.assertEqual(b[sw, "debounce-interval"], liddt.cells(50))
        with self.assertRaisesRegex(ValueError, "debounce_ms"):
            liddt.prepare(before, debounce_ms=2000)

    def test_folded_cover_kernel_patch_applies_cleanly(self):
        patch_path = ROOT / "patches/spx-portable-boot/0012-spx-hall-lid-ignore-folded-cover.patch"
        self.assertTrue(patch_path.is_file())
        text = patch_path.read_text()
        for symbol in ("input_spx_set_cover_folded", "input_spx_cover_is_folded", "folded-back", "folded-canvas", "book"):
            self.assertIn(symbol, text)
        ksrc = Path("/mnt/spx-workspace/chromebookOS/out/release-v0.8.0/kernel-build-sources/kernel")
        if (ksrc / "drivers/input/input.c").is_file():
            res = subprocess.run(
                ["patch", "--dry-run", "--batch", "--forward", "--fuzz=0", "-p1", "-d", str(ksrc), "-i", str(patch_path)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)

    def test_enable_and_ssam_lid_conflict_guard(self):
        before = self.fixture()
        enabled = liddt.prepare(before, enable=True)
        b = liddt.dt.properties(libfdt.Fdt(enabled))
        self.assertEqual(b["/gpio-keys-lid", "status"], b"okay\0")

        with tempfile.TemporaryDirectory() as td:
            ksrc = Path(td)
            tabletsw = ksrc / "drivers/platform/surface/surface_aggregator_tabletsw.c"
            tabletsw.parent.mkdir(parents=True)
            tabletsw.write_text("/* stock upstream driver without SW_LID */\n")
            self.assertFalse(liddt.has_ssam_lid_patch(ksrc))
            ok = liddt.prepare(before, enable=True, kernel_source=ksrc)
            self.assertEqual(liddt.dt.properties(libfdt.Fdt(ok))["/gpio-keys-lid", "status"], b"okay\0")

            # Simulate patch-sq2-lid.py marker SQ2_LID_SWITCH_V1.
            tabletsw.write_text("/* SQ2_LID_SWITCH_V1: SSAM SW_LID enabled */\n")
            self.assertTrue(liddt.has_ssam_lid_patch(ksrc))
            with self.assertRaisesRegex(ValueError, "SQ2_LID_SWITCH_V1"):
                liddt.prepare(before, enable=True, kernel_source=ksrc)
            # Disabled mode stays allowed (no active duplicate SW_LID emitter).
            disabled = liddt.prepare(before, enable=False, kernel_source=ksrc)
            self.assertEqual(liddt.dt.properties(libfdt.Fdt(disabled))["/gpio-keys-lid", "status"], b"disabled\0")
            # Explicit override works when intentionally requested.
            forced = liddt.prepare(before, enable=True, kernel_source=ksrc, allow_ssam_conflict=True)
            self.assertEqual(liddt.dt.properties(libfdt.Fdt(forced))["/gpio-keys-lid", "status"], b"okay\0")

    def test_rejects_invalid_board_missing_tlmm_and_duplicates(self):
        with self.assertRaisesRegex(ValueError, "Surface Pro X"):
            liddt.prepare(self.fixture(board="other,board"))
        with self.assertRaisesRegex(ValueError, "tlmm"):
            liddt.prepare(self.fixture(with_tlmm=False))
        with self.assertRaisesRegex(ValueError, "gpio121"):
            liddt.prepare(self.fixture(extra_tlmm='existing-pin { pins = "gpio121"; };'))
        existing_sw = 'gpio-keys-other { compatible = "gpio-keys"; lid { linux,input-type = <5>; linux,code = <0>; }; };'
        with self.assertRaisesRegex(ValueError, "SW_LID"):
            liddt.prepare(self.fixture(extra_root=existing_sw))
        # Second application on already-prepared DTB is also rejected.
        prepared = liddt.prepare(self.fixture())
        with self.assertRaisesRegex(ValueError, "SW_LID"):
            liddt.prepare(prepared)

    def test_overlay_dts_compiles_with_dtc(self):
        for enable in (False, True):
            for active_low in (True, False):
                dts = liddt.emit_dts(enable=enable, active_low=active_low, debounce_ms=20)
                dtbo = subprocess.check_output(["dtc", "-q", "-@", "-I", "dts", "-O", "dtb"], input=dts.encode())
                self.assertGreater(len(dtbo), 64)


if __name__ == "__main__":
    unittest.main()
