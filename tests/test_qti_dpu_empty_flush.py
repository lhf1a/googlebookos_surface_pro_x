"""Exercise actual empty-stack flush/present code with injected DRM boundary failures."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "out/qualcomm-full-stack-20261108"
DISPLAY = Path(os.environ.get("QTI_DISPLAY_SOURCE", str(BASE / "dpu-empty-flush-work/source")))
COMPOSER = Path(os.environ.get("QTI_COMPOSER_SOURCE", str(BASE / "dpu-empty-flush-work/source")))


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


class EmptyFlush(unittest.TestCase):
    def run_source(self, display, composer, original):
        hw = (display / "sdm/libs/core/drm/hw_device_drm.cpp").read_text()
        base = (composer / "composer/hwc_display.cpp").read_text()
        builtin = (composer / "composer/hwc_display_builtin.cpp").read_text()
        validate = block(builtin, "HWC2::Error HWCDisplayBuiltIn::Validate(")
        text = (ROOT / "tests/qti_dpu_empty_flush_test.cpp.in").read_text()
        for marker, source, start in [
            ("@HW_FLUSH@", hw, "DisplayError HWDeviceDRM::Flush("),
            ("@NULL_COMMIT@", hw, "DisplayError HWDeviceDRM::NullCommit("),
            ("@POST_COMMIT@", base, "HWC2::Error HWCDisplay::PostCommitLayerStack("),
            ("@EMPTY_VALIDATE@", validate, "if (layer_set_.empty())"),
        ]:
            extracted = block(source, start)
            text = text.replace(marker, extracted)
            print(marker, hashlib.sha256(extracted.encode()).hexdigest())
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            cpp, binary = temp / "test.cpp", temp / "test"
            cpp.write_text(text)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-parameter", "-fsanitize=address,undefined",
                            "-fno-pie", "-no-pie", "-DORIGINAL=" + str(int(original)),
                            str(cpp), "-o", str(binary)], check=True)
            result = subprocess.run([str(binary)], text=True, capture_output=True, timeout=10,
                                    env=dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                                             UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"))
            if original:
                self.assertEqual(result.returncode, 3, result.stdout + result.stderr)
                for failure in ("EMPTY_VIDEO_NOT_FLUSHED", "FLUSH_PRESENT_MISSING", "FLUSH_ERROR_SWALLOWED"):
                    self.assertIn(failure, result.stdout)
            else:
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())

    def test_original_exposes_three_regressions(self):
        self.run_source(BASE / "sm8350-display-v15/source", BASE / "sm8350-composer-v13/source", True)

    def test_actual_flush_present_errors_and_repeated_transitions(self):
        self.run_source(DISPLAY, COMPOSER, False)


if __name__ == "__main__":
    unittest.main()
