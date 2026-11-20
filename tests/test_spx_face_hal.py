"""Host tests for Surface Pro X OV7251 IR Face IFace AIDL V4 HAL packaging & staging."""
import importlib.util
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import full_image as image
import full_image_face as face


def _load_module(name: str, rel_path: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / rel_path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class FaceHalTests(unittest.TestCase):
    def test_stage_script_syntax_guards_and_features_filter(self):
        stage_path = ROOT / "probe/face/spx-face-stage"
        mode = stat.S_IMODE(stage_path.stat().st_mode)
        self.assertEqual(mode & 0o111, 0o111, "spx-face-stage must be executable")
        text = stage_path.read_text(encoding="utf-8")
        subprocess.run(["sh", "-n"], input=text, text=True, check=True)
        for token in ("spx.face=0", "sq2.camera=0", "sq2.rescue=1", "/sys/bus/i2c/drivers/ov7251"):
            self.assertIn(token, text)

        sample_features = (
            "<permissions>\n"
            '    <feature name="android.hardware.camera" />\n'
            '    <unavailable-feature name="android.hardware.biometrics.face" />\n'
            '    <unavailable-feature name="android.hardware.fingerprint" />\n'
            "</permissions>\n"
        )
        with tempfile.TemporaryDirectory() as td:
            in_xml = Path(td) / "features.xml"
            in_xml.write_text(sample_features, encoding="utf-8")
            filtered = subprocess.check_output(
                [
                    "sed",
                    r'/<unavailable-feature[[:space:]]\+name="android\.hardware\.biometrics\.face"[[:space:]]*\/>/d',
                    str(in_xml),
                ],
                text=True,
            )
        self.assertIn("android.hardware.camera", filtered)
        self.assertIn("android.hardware.fingerprint", filtered)
        self.assertNotIn("android.hardware.biometrics.face", filtered)

    def test_vintf_permission_and_rc_metadata(self):
        manifest_tree = ET.parse(ROOT / "probe/face/android.hardware.biometrics.face-service.spx.xml")
        hal = manifest_tree.getroot().find("./hal")
        self.assertIsNotNone(hal)
        self.assertEqual(hal.attrib.get("format"), "aidl")
        self.assertEqual(hal.findtext("name"), "android.hardware.biometrics.face")
        self.assertEqual(hal.findtext("version"), "4")
        self.assertEqual(hal.findtext("fqname"), "IFace/default")

        perm_tree = ET.parse(ROOT / "probe/face/android.hardware.biometrics.face.xml")
        feats = [el.attrib.get("name") for el in perm_tree.getroot().findall("./feature")]
        self.assertEqual(feats, ["android.hardware.biometrics.face"])

        rc_text = (ROOT / "probe/face/android.hardware.biometrics.face-service.spx.rc").read_text(
            encoding="utf-8"
        )
        for required in (
            "service vendor.face-default /vendor/bin/hw/android.hardware.biometrics.face-service.default",
            "user system",
            "group system camera input",
            "/data/vendor_de/0/facedata",
            "interface aidl android.hardware.biometrics.face.IFace/default",
        ):
            self.assertIn(required, rc_text)

    def test_full_image_face_install_idempotent_and_validates_elf(self):
        elf = bytearray(64)
        elf[:6] = b"\x7fELF\x02\x01"
        elf[18:20] = (183).to_bytes(2, "little")
        payload = {"hal": bytes(elf) + b"hal-body", "cli": bytes(elf) + b"cli-body"}

        bad_elf = bytearray(elf)
        bad_elf[18:20] = (62).to_bytes(2, "little")  # EM_X86_64
        with self.assertRaisesRegex(ValueError, "ARM64"):
            face.install({}, {"init": b""}, {"hal": bytes(bad_elf)}, image.new_record)

        records = {}
        init_base = b"#!/bin/sh\n" + face.STAGE + b"echo tail\n"
        bodies = {"init": init_base}
        meta = face.install(records, bodies, payload, image.new_record)
        self.assertTrue(meta["enabled"])
        self.assertEqual(meta["aidl_version"], 4)
        self.assertEqual(meta["security_class"], "weak")
        self.assertEqual(records[face.HAL_TARGET], image.new_record(0o100755))
        self.assertEqual(records[face.CLI_TARGET], image.new_record(0o100755))
        self.assertEqual(records[face.STAGE_TARGET], image.new_record(0o100755))
        self.assertEqual(records[face.MANIFEST_TARGET], image.new_record(0o100644))
        self.assertEqual(records[face.PERM_TARGET], image.new_record(0o100644))
        self.assertEqual(records[face.RC_TARGET], image.new_record(0o100644))
        self.assertIn(face.STAGE + face.STAGE_HOOK, bodies["init"])

        snap_bodies = dict(bodies)
        snap_records = dict(records)
        face.install(records, bodies, payload, image.new_record)
        self.assertEqual(bodies, snap_bodies)
        self.assertEqual(records, snap_records)

    def test_build_lts_exposes_hall_lid_and_face_flags(self):
        res = subprocess.run(
            [sys.executable, str(ROOT / "scripts/build-lts.py"), "full-image", "--help"],
            text=True,
            capture_output=True,
            check=True,
        )
        for flag in ("--without-hall-lid", "--enable-hall-lid", "--face-hal", "--without-face"):
            self.assertIn(flag, res.stdout)

    def test_host_cpp_asan_ubsan_suite_and_cli_self_test(self):
        builder = _load_module("build_spx_face_hal", "scripts/build-spx-face-hal.py")
        builder.run_host_tests()

        compiler = shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler, "host C++ compiler required")
        with tempfile.TemporaryDirectory(prefix="spx-face-cli-host-") as td:
            cli = Path(td) / "spx-face-cli"
            subprocess.run(
                [
                    compiler,
                    "-std=c++20",
                    "-O1",
                    "-g",
                    "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I" + str(ROOT / "probe/face"),
                    "-I" + str(ROOT / "probe/camera"),
                    str(ROOT / "probe/camera/spx-capture.cpp"),
                    str(ROOT / "probe/face/spx-ir-preprocess.cpp"),
                    str(ROOT / "probe/face/spx-face-crypto.cpp"),
                    str(ROOT / "probe/face/spx-face-engine.cpp"),
                    str(ROOT / "probe/face/spx-lid-monitor.cpp"),
                    str(ROOT / "probe/face/spx-face-cli.cpp"),
                    "-pthread",
                    "-lm",
                    "-o",
                    str(cli),
                ],
                check=True,
            )
            out = subprocess.check_output([str(cli), "--self-test"], text=True)
            self.assertIn("PASS spx-face-cli --self-test", out)

    def test_arm64_ndk_hal_and_cli_build_if_toolchains_available(self):
        if not face.toolchains_available():
            self.skipTest("ARM64 NDK / AIDL toolchains unavailable")
        builder = _load_module("build_spx_face_hal", "scripts/build-spx-face-hal.py")
        with tempfile.TemporaryDirectory(prefix="spx-face-arm64-test-") as td:
            meta = builder.build_hal(Path(td))
            hal_bin = Path(meta["binary"])
            cli_bin = Path(meta["cli_binary"])
            self.assertTrue((Path(td) / "FACE-HAL.json").is_file())
            self.assertEqual(meta["elf"]["e_machine"], "EM_AARCH64")
            self.assertEqual(meta["cli_elf"]["e_machine"], "EM_AARCH64")
            self.assertTrue(hal_bin.is_file() and cli_bin.is_file())
            readelf = shutil.which("llvm-readelf") or shutil.which("readelf")
            if readelf:
                dyn = subprocess.check_output([readelf, "-d", str(hal_bin)], text=True)
                for lib in ("libbinder_ndk.so", "libnativewindow.so", "liblog.so", "libc.so"):
                    self.assertIn(lib, dyn)


if __name__ == "__main__":
    unittest.main()
