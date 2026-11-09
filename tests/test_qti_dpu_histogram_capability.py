"""Exercise actual QTI DPPS boundaries and sampling consumers with fault injection."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "out/qualcomm-full-stack-20261108"
SOURCE = Path(os.environ.get("QTI_DISPLAY_SOURCE", str(BASE / "sm8350-display-v15/source")))

def block(source, start):
    begin = source.index(start)
    left = source.index("{", begin)
    depth, end = 1, left + 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[begin:end]

class HistogramCapability(unittest.TestCase):
    @unittest.skipUnless((SOURCE / "sdm/libs/core/display_builtin.cpp").is_file(),
                         "requires actual adapted QTI display source")
    def test_actual_capability_and_consumers(self):
        config = json.loads((ROOT / "configs/sm8350-display-build.json").read_text())[
            "dpu_histogram_capability"]
        self.assertEqual(hashlib.sha256((ROOT / config["patch"]).read_bytes()).hexdigest(),
                         config["patch_sha256"])
        sources = {}
        for relative, digest in config["patched_sources"].items():
            path = SOURCE / relative
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), digest, relative)
            sources[relative] = path.read_text()
        text = (ROOT / "tests/qti_dpu_histogram_capability_test.cpp.in").read_text()
        mapping = [
            ("@DRM_GET@", "sde-drm/drm_dpps_mgr_imp.cpp", "void DRMDppsManagerImp::GetDppsFeatureInfo("),
            ("@HW_SET@", "sdm/libs/core/drm/hw_peripheral_drm.cpp", "DisplayError HWPeripheralDRM::SetDppsFeature("),
            ("@HW_GET@", "sdm/libs/core/drm/hw_peripheral_drm.cpp", "DisplayError HWPeripheralDRM::GetDppsFeatureInfo("),
            ("@HW_BL@", "sdm/libs/core/drm/hw_peripheral_drm.cpp", "DisplayError HWPeripheralDRM::SetBLScale("),
            ("@INIT@", "sdm/libs/core/display_builtin.cpp", "void DisplayBuiltIn::initColorSamplingState("),
            ("@SUPPORTED@", "sdm/libs/core/display_builtin.cpp", "DisplayError DisplayBuiltIn::IsSupportedOnDisplay("),
            ("@STATE@", "sdm/libs/core/display_builtin.cpp", "DisplayError DisplayBuiltIn::setColorSamplingState("),
            ("@ON@", "sdm/libs/core/display_builtin.cpp", "DisplayError DisplayBuiltIn::colorSamplingOn("),
            ("@OFF@", "sdm/libs/core/display_builtin.cpp", "DisplayError DisplayBuiltIn::colorSamplingOff("),
            ("@HWC_VENDOR@", "composer/hwc_display_builtin.cpp", "HWC2::Error HWCDisplayBuiltIn::SetDisplayedContentSamplingEnabledVndService("),
            ("@HWC_ENABLE@", "composer/hwc_display_builtin.cpp", "HWC2::Error HWCDisplayBuiltIn::SetDisplayedContentSamplingEnabled("),
            ("@HWC_ATTRIBUTES@", "composer/hwc_display_builtin.cpp", "HWC2::Error HWCDisplayBuiltIn::GetDisplayedContentSamplingAttributes("),
            ("@HWC_SAMPLE@", "composer/hwc_display_builtin.cpp", "HWC2::Error HWCDisplayBuiltIn::GetDisplayedContentSample("),
        ]
        for marker, relative, start in mapping:
            text = text.replace(marker, block(sources[relative], start))
        commit = block(sources["sdm/libs/core/display_builtin.cpp"], "DisplayError DisplayBuiltIn::Commit(")
        text = text.replace("@COMMIT_HISTOGRAM@", block(commit, "if (histogramSetup)"))
        original = BASE / "sm8350-display-v14/source/sdm/libs/core/display_builtin.cpp"
        variants = [("fixed", text)]
        if original.exists():
            fixed_init = block(sources["sdm/libs/core/display_builtin.cpp"],
                               "void DisplayBuiltIn::initColorSamplingState(")
            old_init = block(original.read_text(), "void DisplayBuiltIn::initColorSamplingState(")
            variants.insert(0, ("original_init", text.replace(fixed_init, old_init)))
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            for name, source in variants:
                cpp = temp / (name + ".cpp"); binary = temp / name
                cpp.write_text(source)
                subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                                "-Wno-unused-parameter", "-fsanitize=address,undefined",
                                "-fno-pie", "-no-pie", "-pthread", str(cpp), "-o", str(binary)],
                               check=True)
                result = subprocess.run([str(binary)], cwd=temp, text=True, capture_output=True,
                                        timeout=10,
                                        env=dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                                                 UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"))
                if name == "original_init":
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("!display.histogramSetup", result.stderr)
                else:
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    print(result.stdout.strip())

if __name__ == "__main__":
    unittest.main()
