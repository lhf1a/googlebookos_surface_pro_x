"""Verify modern UI, flow guidance, tablet-mode fullscreen launch, and real APK build."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ANDROID_NS = "{http://schemas.android.com/apk/res/android}"


def _find_sdk():
    env_sdk = os.environ.get("SQ2_ANDROID_SDK")
    if env_sdk:
        cand = Path(env_sdk)
        if (cand / "android-35/android.jar").is_file():
            return cand
    for cand in (
        Path("/mnt/spx-workspace/toolchains/sq2-graphics/sdk"),
        Path("/root/.cache/sq2-graphics/sdk"),
    ):
        try:
            if (cand / "android-35/android.jar").is_file() and (cand / "android-15/aapt2").is_file():
                return cand
        except PermissionError:
            continue
    return None


class TouchCalibrationUiFullscreenTest(unittest.TestCase):
    def test_manifest_enables_tablet_fullscreen_and_config_preservation(self):
        manifest_path = ROOT / "probe/touch-calibration/AndroidManifest.xml"
        tree = ET.parse(manifest_path)
        root = tree.getroot()
        self.assertEqual(root.attrib.get("package"), "org.surface.touchcalibration.unified")

        app = root.find("application")
        self.assertIsNotNone(app)
        self.assertEqual(
            app.attrib.get(ANDROID_NS + "theme"),
            "@android:style/Theme.DeviceDefault.NoActionBar.Fullscreen",
        )
        props = {
            p.attrib.get(ANDROID_NS + "name"): p.attrib.get(ANDROID_NS + "value")
            for p in app.findall("property")
        }
        self.assertEqual(
            props.get("android.window.PROPERTY_COMPAT_ALLOW_USER_ASPECT_RATIO_FULLSCREEN_OVERRIDE"),
            "true",
        )

        act = app.find("activity")
        self.assertIsNotNone(act)
        self.assertEqual(act.attrib.get(ANDROID_NS + "screenOrientation"), "landscape")
        self.assertEqual(act.attrib.get(ANDROID_NS + "resizeableActivity"), "true")
        self.assertEqual(act.attrib.get(ANDROID_NS + "launchMode"), "singleTop")
        cfg = set((act.attrib.get(ANDROID_NS + "configChanges") or "").split("|"))
        for required in ("orientation", "screenSize", "smallestScreenSize", "screenLayout", "keyboard", "uiMode"):
            self.assertIn(required, cfg)

    def test_boot_helper_requests_fullscreen_windowing_mode(self):
        helper = ROOT / "probe/test2/touch-calibration-boot"
        text = helper.read_text()
        self.assertIn("--windowingMode 1", text)
        subprocess.run(["sh", "-n", str(helper)], check=True)

    def test_main_activity_has_modern_ui_and_fullscreen_lifecycle(self):
        src = (ROOT / "probe/touch-calibration/MainActivity.java").read_text()
        for token in (
            "WindowInsetsController",
            "BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE",
            "LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS",
            "requestFullscreenMode(FULLSCREEN_MODE_REQUEST_ENTER",
            "onMultiWindowModeChanged",
            "onWindowFocusChanged",
            "class ThemeTokens",
            "class StepperView",
            "class CountdownRingView",
            "class HoldPillButton",
            "class SwipeMonitorView",
            "class GesturePadView",
            "drawFloatingHud",
            "drawPrecisionTarget",
        ):
            self.assertIn(token, src)

    def test_apk_builds_and_verifies_when_sdk_present(self):
        sdk = _find_sdk()
        if sdk is None:
            self.skipTest("Android SDK not available in this environment")
        spec = importlib.util.spec_from_file_location(
            "build_touch_calibration", ROOT / "scripts/build-touch-calibration.py"
        )
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        old_sdk = os.environ.get("SQ2_ANDROID_SDK")
        old_ks = os.environ.get("SQ2_CALIBRATION_KEYSTORE")
        try:
            with tempfile.TemporaryDirectory(prefix="touch-cal-apk-test-") as td:
                out = Path(td) / "out"
                os.environ["SQ2_ANDROID_SDK"] = str(sdk)
                os.environ["SQ2_CALIBRATION_KEYSTORE"] = str(Path(td) / "test.keystore")
                files, audit = mod.build(ROOT, out)
                self.assertIn("etc/sq2-touch-calibration/wizard.apk", files)
                apk_path = out / "wizard.apk"
                self.assertTrue(apk_path.is_file())
                with zipfile.ZipFile(apk_path, "r") as zf:
                    names = set(zf.namelist())
                    self.assertIn("AndroidManifest.xml", names)
                    self.assertIn("classes.dex", names)
                badging = subprocess.check_output(
                    [str(sdk / "android-15/aapt2"), "dump", "badging", str(apk_path)],
                    text=True,
                )
                self.assertIn("package: name='org.surface.touchcalibration.unified'", badging)
                self.assertIn("targetSdkVersion:'35'", badging)
                self.assertIn("probe/touch-calibration/MainActivity.java", audit["source_sha256"])
        finally:
            if old_sdk is None:
                os.environ.pop("SQ2_ANDROID_SDK", None)
            else:
                os.environ["SQ2_ANDROID_SDK"] = old_sdk
            if old_ks is None:
                os.environ.pop("SQ2_CALIBRATION_KEYSTORE", None)
            else:
                os.environ["SQ2_CALIBRATION_KEYSTORE"] = old_ks


if __name__ == "__main__":
    unittest.main()
