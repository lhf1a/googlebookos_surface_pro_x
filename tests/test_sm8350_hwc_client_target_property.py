"""Execute the real QTI output producer with poisoned fields and conversion failures."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "out/qualcomm-full-stack-20261108"
SOURCE = Path(os.environ.get("QTI_COMPOSER_SOURCE", str(BASE / "sm8350-composer-v10/source")))
PATCH = ROOT / "patches/sm8350-hwc-client-target-property.patch"

def method(source):
    start = source.index("HWC2::Error HWCDisplay::GetClientTargetProperty(")
    left = source.index("{", start)
    depth, end = 1, left + 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[start:end]

class ClientTargetPropertyTest(unittest.TestCase):
    @unittest.skipUnless((SOURCE / "composer/hwc_display.cpp").is_file(),
                         "requires actual QTI composer source")
    def test_actual_output_and_original_negative_control(self):
        config = json.loads((ROOT / "configs/sm8350-display-reference.json").read_text())
        pin = config["client_target_property_patch"]
        self.assertEqual(hashlib.sha256(PATCH.read_bytes()).hexdigest(), pin["sha256"])
        source = (SOURCE / "composer/hwc_display.cpp").read_text()
        digest = hashlib.sha256(source.encode()).hexdigest()
        entry = pin["files"][0]
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            (temp / "composer").mkdir()
            actual = temp / entry["path"]
            actual.write_text(source)
            # Later independent patches may touch the same source file, but not this method.
            for key in ("empty_layer_flush_patch", "empty_layer_output_patch"):
                later = config.get(key)
                item = next((item for item in later["files"] if item["path"] == entry["path"]), None) if later else None
                if not item or digest != item["result_sha256"]:
                    continue
                self.assertEqual(hashlib.sha256((ROOT / later["patch"]).read_bytes()).hexdigest(),
                                 later["sha256"])
                for item in later["files"]:
                    target = temp / item["path"]
                    if not target.exists():
                        target.write_bytes((SOURCE / item["path"]).read_bytes())
                    self.assertEqual(hashlib.sha256(target.read_bytes()).hexdigest(), item["result_sha256"])
                subprocess.run(["patch", "--reverse", "--batch", "--fuzz=0", "-p1", "-i",
                                str(ROOT / later["patch"])], cwd=temp, check=True,
                               stdout=subprocess.PIPE)
                digest = hashlib.sha256(actual.read_bytes()).hexdigest()
            self.assertIn(digest, [entry["base_sha256"], entry["result_sha256"]])
            reverse = digest == entry["result_sha256"]
            if reverse:
                subprocess.run(["patch", "--reverse", "--batch", "--fuzz=0", "-p1", "-i", str(PATCH)],
                               cwd=temp, check=True, stdout=subprocess.PIPE)
            original = actual.read_text()
            self.assertEqual(hashlib.sha256(original.encode()).hexdigest(), entry["base_sha256"])
            subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(PATCH)],
                           cwd=temp, check=True, stdout=subprocess.PIPE)
            fixed = actual.read_text()
            self.assertEqual(hashlib.sha256(fixed.encode()).hexdigest(), entry["result_sha256"])
            template = (ROOT / "tests/sm8350_hwc_client_target_property_test.cpp.in").read_text()
            for name, text in [("original", original), ("fixed", fixed)]:
                cpp = temp / (name + ".cpp")
                binary = temp / name
                cpp.write_text(template.replace("@METHOD@", method(text)))
                subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                                "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                                str(cpp), "-o", str(binary)], check=True)
                result = subprocess.run(
                    [str(binary)], cwd=temp, text=True, capture_output=True, timeout=10,
                    env=dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                             UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"))
                if name == "original":
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("property.pixelFormat == PixelFormat::RGBA_8888", result.stderr)
                else:
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    print(result.stdout.strip())

if __name__ == "__main__":
    unittest.main()
