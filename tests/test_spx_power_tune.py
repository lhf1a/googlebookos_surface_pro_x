"""Host unit tests for probe/test2/spx-power-tune-boot and full_image wiring."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import full_image as fi_mod
import runtime_boot_policy as rbp_mod


def _make_fake_sys(base: Path):
    nvme_p = base / "class/nvme/nvme0/power"
    nvme_p.mkdir(parents=True)
    (nvme_p / "pm_qos_latency_tolerance_us").write_text("0\n")

    drv = base / "bus/platform/drivers/dwc3-qcom-legacy"
    w_host = drv / "a4f8800.usb"
    (w_host / "power").mkdir(parents=True)
    (w_host / "power/control").write_text("on\n")
    (w_host / "power/wakeup").write_text("enabled\n")
    c_host = w_host / "a400000.usb"
    (c_host / "power").mkdir(parents=True)
    (c_host / "power/control").write_text("on\n")
    (c_host / "of_node").mkdir(parents=True)
    (c_host / "of_node/dr_mode").write_bytes(b"host\x00")
    xhci0 = c_host / "xhci-hcd.0.auto"
    (xhci0 / "power").mkdir(parents=True)
    (xhci0 / "power/control").write_text("on\n")

    w_otg = drv / "a6f8800.usb"
    (w_otg / "power").mkdir(parents=True)
    (w_otg / "power/control").write_text("on\n")
    (w_otg / "power/wakeup").write_text("enabled\n")
    c_otg = w_otg / "a600000.usb"
    (c_otg / "power").mkdir(parents=True)
    (c_otg / "power/control").write_text("on\n")
    (c_otg / "of_node").mkdir(parents=True)
    (c_otg / "of_node/dr_mode").write_bytes(b"otg\x00")
    xhci1 = c_otg / "xhci-hcd.1.auto"
    (xhci1 / "power").mkdir(parents=True)
    (xhci1 / "power/control").write_text("on\n")

    w_nowake = drv / "a8f8800.usb"
    (w_nowake / "power").mkdir(parents=True)
    (w_nowake / "power/control").write_text("on\n")
    (w_nowake / "power/wakeup").write_text("disabled\n")
    c_nw = w_nowake / "a800000.usb"
    (c_nw / "power").mkdir(parents=True)
    (c_nw / "power/control").write_text("on\n")
    (c_nw / "of_node").mkdir(parents=True)
    (c_nw / "of_node/dr_mode").write_bytes(b"host\x00")


class SpxPowerTuneTests(unittest.TestCase):
    def _run_helper(self, tmp: Path, cmdline: str, extra_env=None):
        sys_root = tmp / "sys"
        if not sys_root.exists():
            _make_fake_sys(sys_root)
        cmd_file = tmp / "cmdline"
        cmd_file.write_text(cmdline + "\n")
        env = dict(os.environ)
        env.update({
            "SQ2_SYS_ROOT": str(sys_root),
            "SQ2_CMDLINE": str(cmd_file),
            "SQ2_POWER_TUNE_LOCK": str(tmp / "lock"),
            "SQ2_POWER_TUNE_SKIP_BOOT_WAIT": "1",
            "SQ2_TEST_PATH": os.environ.get("PATH", "/usr/bin:/bin"),
        })
        if extra_env:
            env.update(extra_env)
        res = subprocess.run(
            ["sh", str(ROOT / "probe/test2/spx-power-tune-boot")],
            env=env,
            capture_output=True,
            text=True,
            check=True,
        )
        return sys_root, res.stdout

    def test_enables_nvme_apst_and_host_only_usb_rpm(self):
        with tempfile.TemporaryDirectory() as td:
            sys_root, out = self._run_helper(
                Path(td),
                "sq2.native=ssd nvme_core.default_ps_max_latency_us=0",
            )
            self.assertIn("NVME_APST_ENABLED:nvme0:25000us", out)
            self.assertIn("USB_HOST_RPM_AUTO:a4f8800.usb", out)
            self.assertEqual(
                (sys_root / "class/nvme/nvme0/power/pm_qos_latency_tolerance_us").read_text().strip(),
                "25000",
            )
            self.assertEqual(
                (sys_root / "bus/platform/drivers/dwc3-qcom-legacy/a4f8800.usb/power/control").read_text().strip(),
                "auto",
            )
            self.assertEqual(
                (sys_root / "bus/platform/drivers/dwc3-qcom-legacy/a4f8800.usb/a400000.usb/power/control").read_text().strip(),
                "auto",
            )
            self.assertEqual(
                (sys_root / "bus/platform/drivers/dwc3-qcom-legacy/a4f8800.usb/a400000.usb/xhci-hcd.0.auto/power/control").read_text().strip(),
                "auto",
            )
            self.assertEqual(
                (sys_root / "bus/platform/drivers/dwc3-qcom-legacy/a6f8800.usb/power/control").read_text().strip(),
                "on",
            )
            self.assertEqual(
                (sys_root / "bus/platform/drivers/dwc3-qcom-legacy/a6f8800.usb/a600000.usb/xhci-hcd.1.auto/power/control").read_text().strip(),
                "on",
            )
            self.assertEqual(
                (sys_root / "bus/platform/drivers/dwc3-qcom-legacy/a8f8800.usb/power/control").read_text().strip(),
                "on",
            )

    def test_respects_rescue_and_opt_out_flags(self):
        with tempfile.TemporaryDirectory() as td:
            sys_root, out = self._run_helper(Path(td), "sq2.rescue=1")
            self.assertEqual(out, "")
            self.assertEqual(
                (sys_root / "class/nvme/nvme0/power/pm_qos_latency_tolerance_us").read_text().strip(),
                "0",
            )
            self.assertEqual(
                (sys_root / "bus/platform/drivers/dwc3-qcom-legacy/a4f8800.usb/power/control").read_text().strip(),
                "on",
            )

        with tempfile.TemporaryDirectory() as td:
            sys_root, out = self._run_helper(Path(td), "sq2.nvme_apst=0 sq2.usb_rpm=0")
            self.assertIn("NVME_APST_DISABLED_BY_CMDLINE", out)
            self.assertIn("USB_RPM_DISABLED_BY_CMDLINE", out)
            self.assertEqual(
                (sys_root / "class/nvme/nvme0/power/pm_qos_latency_tolerance_us").read_text().strip(),
                "0",
            )
            self.assertEqual(
                (sys_root / "bus/platform/drivers/dwc3-qcom-legacy/a4f8800.usb/power/control").read_text().strip(),
                "on",
            )

    def test_runtime_boot_policy_and_full_image_wiring(self):
        bodies = {"init": b"header\n" + fi_mod.ANCHOR + b"tail\n"}
        records = {}
        fi_mod.install_power_tune(records, bodies)
        self.assertIn("bin/spx-power-tune-boot", bodies)
        self.assertIn(fi_mod.POWER_TUNE_HOOK, bodies["init"])
        rbp_mod.apply({"bin/spx-power-tune-boot": bodies["bin/spx-power-tune-boot"]})
        fi_src = (ROOT / "scripts/full_image.py").read_text("utf-8")
        self.assertIn("    install_power_tune(records, bodies)\n", fi_src)
        self.assertIn("probe/lte/lte-framework-boot", fi_src)


if __name__ == "__main__":
    unittest.main()
