"""Verify SQ2 Wi-Fi legacy HAL SoftAP/nl80211 support and runtime assembly."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import full_image as image
import full_image_camera as camera
import full_image_rescue as rescue


def _find_ndk_clang():
    for cand in (
        Path("/mnt/spx-workspace/toolchains/sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang"),
        Path("/root/.cache/sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang"),
    ):
        if cand.is_file():
            return cand
    return None


class Sq2WifiSoftApTests(unittest.TestCase):
    def test_wifi_hal_source_abi_and_dynamic_nl80211(self):
        src = (ROOT / "probe/test2/sq2-wifi-hal.cpp").read_text()
        for expected in (
            "0x140",
            "0x1b0",
            "0x328",
            "0x330",
            "0x448",
            "NL80211_CMD_NEW_INTERFACE",
            "NL80211_CMD_DEL_INTERFACE",
            "NL80211_CMD_REQ_SET_REG",
            "NL80211_CMD_GET_WIPHY",
            "WIFI_FEATURE_SOFT_AP",
            "WIFI_FEATURE_AP_STA",
            "phy80211/index",
            "/sys/class/ieee80211",
        ):
            self.assertIn(expected, src)
        self.assertNotIn('"phy0"', src)

    def test_wifi_hal_compiles_with_ndk_when_headers_present(self):
        clang = _find_ndk_clang()
        headers = ROOT / "out/connectivity/source/headers"
        if clang is None or not (headers / "wifi_hal.h").is_file():
            self.skipTest("Android NDK clang or fetched AOSP wifi_hal.h headers not present")

        with tempfile.TemporaryDirectory() as td:
            out = Path(td)
            subprocess.run(
                [
                    str(clang),
                    "-c",
                    "-fPIC",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(ROOT / "probe/test2/sq2-wifi-driver.c"),
                    "-o",
                    str(out / "wifi-driver.o"),
                ],
                check=True,
            )
            subprocess.run(
                [
                    str(clang) + "++",
                    "-shared",
                    "-fPIC",
                    "-O2",
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-static-libstdc++",
                    "-I" + str(headers),
                    str(ROOT / "probe/test2/sq2-wifi-hal.cpp"),
                    str(out / "wifi-driver.o"),
                    "-llog",
                    "-o",
                    str(out / "libsq2-wifi-hal.so"),
                ],
                check=True,
            )
            so = out / "libsq2-wifi-hal.so"
            self.assertTrue(so.is_file())
            self.assertGreater(so.stat().st_size, 0)

    def test_assemble_runtime_accepts_compiled_wifi_hal_helper(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            runtime = root / "full-image-runtime"
            wifi_hal_rel = "etc/googlebook-security/lib64/libsq2-wifi-hal.so"
            bodies = {
                "init": b"#!/bin/sh\n" + rescue.LEGACY_RESCUE_BLOCK + camera.STAGE + image.ANCHOR,
                "etc/shadow": b"identity fixture",
                "bin/dropbear": b"ssh fixture",
                "bin/start-ssh": b"start fixture",
                wifi_hal_rel: b"old wifi hal",
            }
            for name, data in bodies.items():
                p = runtime / "files" / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(data)
            (runtime / "records.json").write_text(
                json.dumps({n: image.new_record(0o100644) for n in bodies})
            )
            loader = image.module

            def load(name, filename):
                if filename == "build-touch-calibration.py":
                    return SimpleNamespace(
                        build=lambda _root: (
                            {"etc/sq2-touch-calibration/wizard.apk": (0o100644, b"wizard")},
                            {},
                        )
                    )
                if filename == "prepare-sq2-primary-audio.py":
                    return SimpleNamespace(install=lambda _bodies: None)
                return loader(name, filename)

            output = root / "runtime.gz"
            expected = b"\x7fELF" + b"new sq2 wifi hal"
            with patch.object(image, "module", side_effect=load):
                image.assemble_runtime(
                    root,
                    output,
                    graphics=None,
                    compiled_helpers={wifi_hal_rel: expected},
                )
            _, result = image.records_from_gzip(output)
            self.assertEqual(result[wifi_hal_rel], expected)


if __name__ == "__main__":
    unittest.main()
