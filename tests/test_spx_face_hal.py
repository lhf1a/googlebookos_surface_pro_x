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
            "restorecon /vendor/overlay/spx-face-overlay.apk",
        ):
            self.assertIn(required, rc_text)

        overlay_manifest = ET.parse(ROOT / "probe/face/overlay/AndroidManifest.xml").getroot()
        self.assertEqual(overlay_manifest.attrib.get("package"), "org.surface.face.overlay")
        overlay_tag = overlay_manifest.find("./overlay")
        self.assertIsNotNone(overlay_tag)
        android_ns = "{http://schemas.android.com/apk/res/android}"
        self.assertEqual(overlay_tag.attrib.get(f"{android_ns}targetPackage"), "com.android.settings")
        self.assertEqual(overlay_tag.attrib.get(f"{android_ns}isStatic"), "true")
        self.assertEqual(overlay_tag.attrib.get(f"{android_ns}priority"), "999")
        self.assertEqual(overlay_tag.attrib.get(f"{android_ns}resourcesMap"), "@xml/overlays")

        overlays_xml = ET.parse(ROOT / "probe/face/overlay/res/xml/overlays.xml").getroot()
        overlay_targets = {el.attrib.get("target"): el.attrib.get("value") for el in overlays_xml.findall("./item")}
        self.assertEqual(overlay_targets.get("bool/config_face_settings_should_manage_preview"),
                         "@bool/config_face_settings_should_manage_preview")
        self.assertEqual(overlay_targets.get("dimen/face_preview_scale"), "@dimen/face_preview_scale")
        self.assertEqual(overlay_targets.get("dimen/face_preview_translate_x"), "@dimen/face_preview_translate_x")
        self.assertEqual(overlay_targets.get("dimen/face_preview_translate_y"), "@dimen/face_preview_translate_y")
        self.assertEqual(overlay_targets.get("layout/face_enrolling"), "@layout/face_enrolling")
        for req_str in (
            "face_enrolling_title",
            "face_enrolling_center_head",
            "face_enrolling_description",
            "face_enrolling_turn_head_to_arrow",
            "face_enrolling_turned_too_far",
        ):
            self.assertEqual(overlay_targets.get(f"string/{req_str}"), f"@string/{req_str}")
        for req_id in (
            "setup_wizard_layout",
            "square_frame_layout",
            "texture_view",
            "circle_view",
            "face_smiley",
            "distance_indicator_view",
            "indicator_view",
            "error_text",
        ):
            self.assertEqual(overlay_targets.get(f"id/{req_id}"), f"@id/{req_id}")
        for private_res in ("drawable/spx_face_preview_bg", "drawable/spx_face_preview_border"):
            self.assertNotIn(private_res, overlay_targets)

        layout_root = ET.parse(ROOT / "probe/face/overlay/res/layout/face_enrolling.xml").getroot()
        views_by_id = {}
        rounded_container = None
        for el in layout_root.iter():
            vid = el.attrib.get(f"{android_ns}id")
            if vid:
                views_by_id[vid] = el
            if el.attrib.get(f"{android_ns}clipToOutline") == "true":
                rounded_container = el
        self.assertIsNotNone(rounded_container)
        self.assertEqual(rounded_container.attrib.get(f"{android_ns}background"), "@drawable/spx_face_preview_bg")
        self.assertTrue(any(
            child.attrib.get(f"{android_ns}id") == "@id/texture_view"
            and child.attrib.get(f"{android_ns}visibility") not in ("invisible", "gone")
            for child in rounded_container
        ))
        self.assertTrue(any(
            child.attrib.get(f"{android_ns}background") == "@drawable/spx_face_preview_border"
            for child in rounded_container
        ))
        for hidden_id in (
            "@id/circle_view",
            "@id/face_smiley",
            "@id/distance_indicator_view",
            "@id/indicator_view",
        ):
            self.assertIn(hidden_id, views_by_id)
            self.assertEqual(views_by_id[hidden_id].attrib.get(f"{android_ns}visibility"), "invisible")

        cfg_root = ET.parse(ROOT / "probe/face/overlay/res/values/config.xml").getroot()
        bools = {el.attrib.get("name"): el.text for el in cfg_root.findall("./bool")}
        items = {el.attrib.get("name"): el.text for el in cfg_root.findall("./item")}
        self.assertEqual(bools.get("config_face_settings_should_manage_preview"), "false")
        self.assertEqual(items.get("face_preview_scale"), "1.0")
        self.assertEqual(items.get("face_preview_translate_x"), "0.0")
        self.assertEqual(items.get("face_preview_translate_y"), "0.0")

        stage_text = (ROOT / "probe/face/spx-face-stage").read_text(encoding="utf-8")
        self.assertIn('"$source/spx-face-overlay.apk"', stage_text)
        self.assertIn('"$vendor/overlay/spx-face-overlay.apk"', stage_text)

        hal_src = (ROOT / "probe/face/spx-face-hal.cpp").read_text(encoding="utf-8")
        gf_start = hal_src.index("getFeatures()")
        gf_end = hal_src.index("setFeature(", gf_start)
        get_features_body = hal_src[gf_start:gf_end]
        self.assertIn("Feature::REQUIRE_ATTENTION", get_features_body)
        self.assertNotIn("Feature::REQUIRE_DIVERSE_POSES", get_features_body)

        sg_start = hal_src.index("shouldGateOperation(")
        sg_end = hal_src.index("registerCancelFlag(", sg_start)
        self.assertIn("syncContextLocked(*ctx)", hal_src[sg_start:sg_end])

    def test_full_image_face_install_idempotent_and_validates_elf(self):
        elf = bytearray(64)
        elf[:6] = b"\x7fELF\x02\x01"
        elf[18:20] = (183).to_bytes(2, "little")
        payload = {"hal": bytes(elf) + b"hal-body", "cli": bytes(elf) + b"cli-body", "overlay": b"APK-TEST"}

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
        self.assertIsNotNone(meta["overlay_sha256"])
        self.assertEqual(records[face.HAL_TARGET], image.new_record(0o100755))
        self.assertEqual(records[face.CLI_TARGET], image.new_record(0o100755))
        self.assertEqual(records[face.OVERLAY_TARGET], image.new_record(0o100644))
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

    def test_build_face_overlay_apk_if_sdk_available(self):
        overlay_builder = _load_module("build_spx_face_overlay", "scripts/build-spx-face-overlay.py")
        if not overlay_builder.sdk_tools_available():
            self.skipTest("Android SDK overlay tools unavailable")
        with tempfile.TemporaryDirectory(prefix="spx-face-overlay-test-") as td:
            apk = overlay_builder.build_overlay(Path(td), ephemeral_keystore=True)
            self.assertTrue(apk.is_file())
            aapt2 = overlay_builder.DEFAULT_SDK / "android-15/aapt2"
            dumped = subprocess.check_output([str(aapt2), "dump", "resources", str(apk)], text=True)
            self.assertIn("config_face_settings_should_manage_preview", dumped)
            self.assertIn("false", dumped)
            self.assertIn("face_preview_translate_x", dumped)
            self.assertIn("face_preview_translate_y", dumped)
            self.assertIn("layout/face_enrolling", dumped)
            self.assertIn("drawable/spx_face_preview_bg", dumped)
            self.assertIn("drawable/spx_face_preview_border", dumped)
            self.assertIn("xml/overlays", dumped)
            self.assertIn("id/texture_view", dumped)
            self.assertIn("string/face_enrolling_title", dumped)
            self.assertIn("string/face_enrolling_turn_head_to_arrow", dumped)


if __name__ == "__main__":
    unittest.main()
