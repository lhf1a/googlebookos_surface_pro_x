"""Replay the real destroy/empty-validate/query/HIDL-count path, including old layer-0 leak."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "out/qualcomm-full-stack-20261108"
SOURCE = Path(os.environ.get("QTI_COMPOSER_SOURCE", str(BASE / "sm8350-composer-v12/source")))

def block(source, start):
    begin = source.index(start); left = source.index("{", begin); depth, end = 1, left + 1
    while depth:
        if source[end] == "{": depth += 1
        elif source[end] == "}": depth -= 1
        end += 1
    return source[begin:end]

class EmptyLayerOutput(unittest.TestCase):
    @unittest.skipUnless((SOURCE / "composer/hwc_display_builtin.cpp").is_file(),
                         "requires actual QTI composer source")
    def test_actual_empty_stack_and_original_layer_zero(self):
        config = json.loads((ROOT / "configs/sm8350-display-reference.json").read_text())
        pin = config["empty_layer_output_patch"]
        patch = ROOT / pin["patch"]
        self.assertEqual(hashlib.sha256(patch.read_bytes()).hexdigest(), pin["sha256"])
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory); (temp / "composer").mkdir()
            for item in pin["files"]:
                shutil.copy2(SOURCE / item["path"], temp / item["path"])
            first = pin["files"][0]
            digest = hashlib.sha256((temp / first["path"]).read_bytes()).hexdigest()
            later = config.get("empty_layer_flush_patch")
            later_item = next((item for item in later["files"] if item["path"] == first["path"]), None) if later else None
            if later_item and digest == later_item["result_sha256"]:
                self.assertEqual(hashlib.sha256((ROOT / later["patch"]).read_bytes()).hexdigest(),
                                 later["sha256"])
                for item in later["files"]:
                    self.assertEqual(hashlib.sha256((temp / item["path"]).read_bytes()).hexdigest(),
                                     item["result_sha256"])
                subprocess.run(["patch", "--reverse", "--batch", "--fuzz=0", "-p1", "-i",
                                str(ROOT / later["patch"])], cwd=temp, check=True, stdout=subprocess.PIPE)
                digest = hashlib.sha256((temp / first["path"]).read_bytes()).hexdigest()
            if digest == first["result_sha256"]:
                subprocess.run(["patch", "--reverse", "--batch", "--fuzz=0", "-p1", "-i", str(patch)],
                               cwd=temp, check=True, stdout=subprocess.PIPE)
            versions = {}
            for name in ("original", "fixed"):
                if name == "fixed":
                    subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(patch)],
                                   cwd=temp, check=True, stdout=subprocess.PIPE)
                contents = {}
                for item in pin["files"]:
                    path = temp / item["path"]
                    self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),
                                     item["base_sha256" if name == "original" else "result_sha256"])
                    contents[item["path"]] = path.read_text()
                versions[name] = contents
            for name, sources in versions.items():
                base = sources["composer/hwc_display.cpp"]
                builtin = sources["composer/hwc_display_builtin.cpp"]
                template = (ROOT / "tests/sm8350_hwc_empty_layer_output_test.cpp.in").read_text()
                for marker, source, start in [
                    ("@DESTROY@", base, "HWC2::Error HWCDisplay::DestroyLayer("),
                    ("@CHANGED@", base, "HWC2::Error HWCDisplay::GetChangedCompositionTypes("),
                    ("@REQUESTS@", base, "HWC2::Error HWCDisplay::GetDisplayRequests("),
                    ("@INTERNAL_VALIDATE@", builtin,
                     "HWC2::Error HWCDisplayBuiltIn::PresentAndOrGetValidateDisplayOutput("),
                    ("@TRANSPORT@", (SOURCE / "composer/QtiComposerClient.cpp").read_text(),
                     "Error QtiComposerClient::CommandReader::validateDisplay("),
                ]:
                    template = template.replace(marker, block(source, start))
                validate = block(builtin, "HWC2::Error HWCDisplayBuiltIn::Validate(")
                template = template.replace("@EMPTY_VALIDATE@", block(validate, "if (layer_set_.empty())"))
                cpp = temp / (name + ".cpp"); binary = temp / name
                cpp.write_text(template)
                subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                                "-Wno-unused-parameter", "-fsanitize=address,undefined",
                                "-fno-pie", "-no-pie", str(cpp), "-o", str(binary)], check=True)
                result = subprocess.run([str(binary)], cwd=temp, text=True, capture_output=True, timeout=10,
                                        env=dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                                                 UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"))
                if name == "original":
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("LEAKED_LAYER_ID=0", result.stdout)
                else:
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    print(result.stdout.strip())

if __name__ == "__main__": unittest.main()
