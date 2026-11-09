"""Actual QTI fence reader/callers with synthetic sync metadata and real FD lifetime checks."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tempfile
import unittest
from tests.test_sm8350_composer_command_errors import block
ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(os.environ.get("QTI_ACQUIRE_SOURCE",
    ROOT / "out/qualcomm-full-stack-20261108/sm8350-composer-v20/source"))

class AcquireFence(unittest.TestCase):
    @unittest.skipUnless((SOURCE / "composer/QtiComposerClient.cpp").exists(), "requires pinned source")
    def test_actual_reader_and_three_callers(self):
        pin = json.loads((ROOT / "configs/sm8350-display-reference.json").read_text())["composer_acquire_fence_patch"]
        patch = ROOT / pin["patch"]
        self.assertEqual(hashlib.sha256(patch.read_bytes()).hexdigest(), pin["sha256"])
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            states = []
            for item in pin["files"]:
                path = temp / item["path"]
                path.parent.mkdir(exist_ok=True)
                path.write_bytes((SOURCE / item["path"]).read_bytes())
                digest = hashlib.sha256(path.read_bytes()).hexdigest()
                self.assertIn(digest, [item["base_sha256"], item["result_sha256"]])
                states.append(digest == item["result_sha256"])
            self.assertEqual(len(set(states)), 1, "Mixed source revisions")
            if all(states):
                subprocess.run(["patch", "--reverse", "--batch", "--fuzz=0", "-p1", "-i", str(patch)],
                               cwd=temp, capture_output=True, text=True, check=True)
            for item in pin["files"]:
                self.assertEqual(hashlib.sha256((temp/item["path"]).read_bytes()).hexdigest(), item["base_sha256"])
            for version in ("original", "fixed"):
                if version == "fixed":
                    subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(patch)], cwd=temp,
                                   capture_output=True, text=True, check=True)
                    for item in pin["files"]:
                        self.assertEqual(hashlib.sha256((temp/item["path"]).read_bytes()).hexdigest(), item["result_sha256"])
                header = (temp / "composer/QtiComposerCommandBuffer.h").read_text()
                client = (temp / "composer/QtiComposerClient.cpp").read_text()
                cpp = (ROOT / "tests/sm8350_composer_acquire_fence_test.cpp.in").read_text()
                cpp = cpp.replace("@READ_FENCE@", block(header, ("void" if version=="original" else "Error") + " readFence("))
                for marker, method in (("@LAYER@", "parseSetLayerBuffer"),("@TARGET@", "parseSetClientTarget"),("@OUTPUT@", "parseSetOutputBuffer")):
                    cpp = cpp.replace(marker, block(client, "bool QtiComposerClient::CommandReader::" + method + "("))
                # Original drops all nine invalid/resource cases per command.
                cpp = cpp.replace("EXPECTED_MISMATCHES", "27" if version=="original" else "0")
                cpp = cpp.replace("EXPECTED_SHORT_MISMATCHES", "1" if version=="original" else "0")
                source, binary = temp/(version+".cpp"), temp/version
                source.write_text(cpp)
                result = subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                    "-fsanitize=address,undefined", "-fno-pie", "-no-pie", str(source), "-o", str(binary)],
                    capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                print(version, result.stdout.strip())
