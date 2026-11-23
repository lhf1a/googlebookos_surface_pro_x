"""Exercise Android GNSS AIDL V4 HAL build, staging, and initramfs integration."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import full_image as image
import full_image_gnss as gnss


def _make_fake_arm64_elf(tag: bytes = b"gnss-hal") -> bytes:
    buf = bytearray(64)
    buf[0:4] = b"\x7fELF"
    buf[4] = 2  # ELFCLASS64
    buf[5] = 1  # ELFDATA2LSB
    buf[6] = 1
    buf[16:18] = (3).to_bytes(2, "little")    # ET_DYN
    buf[18:20] = (183).to_bytes(2, "little")  # EM_AARCH64
    return bytes(buf) + tag


class FullImageGnssTests(unittest.TestCase):
    def test_stage_script_has_valid_POSIX_shell_syntax_and_unmasks_gps_feature(self):
        script = (ROOT / "probe/gnss/spx-gnss-stage").read_bytes()
        proc = subprocess.run(["sh", "-n"], input=script, capture_output=True)
        self.assertEqual(proc.returncode, 0, proc.stderr.decode("utf-8", "ignore"))
        text = script.decode()
        self.assertIn("spx.gnss=0", text)
        self.assertIn("android.hardware.location.gps", text)
        self.assertIn("mount --bind", text)
        self.assertIn("android.hardware.gnss-service.example", text)
        self.assertIn("LONGTERM_PSDS_SERVER_1=", text)

        manifest = (ROOT / "probe/gnss/android.hardware.gnss-service.spx.xml").read_text()
        self.assertIn("android.hardware.gnss", manifest)
        self.assertIn("<version>4</version>", manifest)
        self.assertIn("IGnss/default", manifest)

        rc_text = (ROOT / "probe/gnss/android.hardware.gnss-service.spx.rc").read_text()
        self.assertIn("restorecon /vendor/bin/hw/android.hardware.gnss-service.example", rc_text)
        self.assertIn("user gps", rc_text)
        self.assertIn("interface aidl android.hardware.gnss.IGnss/default", rc_text)

    def test_install_injects_stage_hook_and_payloads_idempotently(self):
        records = {
            "init": image.new_record(0o100755),
        }
        bodies = {
            "init": b"#!/bin/sh\n" + gnss.STAGE + image.ANCHOR,
        }
        fake_hal = _make_fake_arm64_elf()
        meta = gnss.install(records, bodies, fake_hal, image.new_record)
        self.assertTrue(meta["enabled"])
        self.assertEqual(meta["aidl_version"], 4)
        self.assertEqual(bodies[gnss.HAL_TARGET], fake_hal)
        self.assertEqual(records[gnss.HAL_TARGET][1][1], 0o100755)
        self.assertEqual(records[gnss.STAGE_TARGET][1][1], 0o100755)
        self.assertEqual(records[gnss.MANIFEST_TARGET][1][1], 0o100644)
        self.assertEqual(records[gnss.RC_TARGET][1][1], 0o100644)
        self.assertLess(bodies["init"].index(gnss.STAGE), bodies["init"].index(gnss.STAGE_HOOK))

        before = dict(bodies)
        gnss.install(records, bodies, fake_hal, image.new_record)
        self.assertEqual(bodies, before)

    def test_rejects_non_arm64_hal_binary(self):
        with tempfile.TemporaryDirectory() as td:
            bad = Path(td) / "bad-hal"
            bad.write_bytes(b"not-an-elf")
            with self.assertRaisesRegex(ValueError, "ARM64 ELF64"):
                gnss.load(gnss_hal_path=bad)

    def test_build_hal_compiles_arm64_aidl_v4_binary_when_toolchains_present(self):
        if not gnss.toolchains_available():
            self.skipTest("NDK/hardware-interfaces toolchain unavailable")
        spec = importlib.util.spec_from_file_location(
            "build_spx_gnss_hal", ROOT / "scripts/build-spx-gnss-hal.py"
        )
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        with tempfile.TemporaryDirectory(prefix="spx-gnss-hal-unit-") as td:
            meta = builder.build_hal(Path(td))
            binary = Path(meta["binary"])
            self.assertTrue(binary.is_file())
            self.assertEqual(meta["elf"]["e_machine"], "EM_AARCH64")
            needed = subprocess.check_output(
                ["readelf", "-d", str(binary)], text=True
            )
            self.assertIn("libbinder_ndk.so", needed)
            self.assertIn("liblog.so", needed)
            with patch.object(gnss, "toolchains_available", return_value=True):
                loaded = gnss.load(gnss_hal_path=binary)
                self.assertEqual(loaded, binary.read_bytes())


if __name__ == "__main__":
    unittest.main()
