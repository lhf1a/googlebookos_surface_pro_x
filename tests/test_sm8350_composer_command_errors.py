"""Run actual QTI parser/three transport methods with injected FMQ storage failures."""
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
SOURCE = Path(os.environ.get("QTI_COMPOSER_SOURCE", str(BASE / "sm8350-composer-v16/source")))

def block(source, token):
    start = source.index(token)
    left = source.index("{", start)
    depth, end = 1, left + 1
    while depth:
        if source[end] == "{": depth += 1
        elif source[end] == "}": depth -= 1
        end += 1
    return source[start:end]

class CommandErrors(unittest.TestCase):
    @unittest.skipUnless((SOURCE / "composer/QtiComposerClient.cpp").is_file(),
                         "requires pinned QTI composer source")
    def test_actual_parser_and_transport_error_contract(self):
        pin = json.loads((ROOT / "configs/sm8350-display-reference.json").read_text())["composer_command_errors_patch"]
        patch = ROOT / pin["patch"]
        self.assertEqual(hashlib.sha256(patch.read_bytes()).hexdigest(), pin["sha256"])
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            target = temp / pin["files"][0]["path"]
            target.parent.mkdir()
            shutil.copy2(SOURCE / pin["files"][0]["path"], target)
            digest = lambda: hashlib.sha256(target.read_bytes()).hexdigest()
            if digest() == pin["files"][0]["result_sha256"]:
                subprocess.run(["patch", "--reverse", "--batch", "--fuzz=0", "-p1", "-i", str(patch)],
                               cwd=temp, check=True, stdout=subprocess.PIPE)
            self.assertEqual(digest(), pin["files"][0]["base_sha256"])
            for version in ("original", "fixed"):
                if version == "fixed":
                    subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(patch)],
                                   cwd=temp, check=True, stdout=subprocess.PIPE)
                    self.assertEqual(digest(), pin["files"][0]["result_sha256"])
                source = target.read_text()
                cpp = (ROOT / "tests/sm8350_composer_command_errors_test.cpp.in").read_text()
                for marker, token in (
                    ("@PARSE@", "Error QtiComposerClient::CommandReader::parse()"),
                    ("@EXECUTE@", "Return<void> QtiComposerClient::executeCommands("),
                    ("@EXECUTE22@", "Return<void> QtiComposerClient::executeCommands_2_2("),
                    ("@EXECUTE23@", "Return<void> QtiComposerClient::executeCommands_2_3(")):
                    cpp = cpp.replace(marker, block(source, token))
                path, binary = temp / (version + ".cpp"), temp / version
                path.write_text(cpp)
                subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                                "-Wno-unused-parameter", "-fsanitize=address,undefined",
                                "-fno-pie", "-no-pie", str(path), "-o", str(binary)], check=True)
                result = subprocess.run([str(binary)], text=True, capture_output=True, timeout=10,
                                        env=dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                                                 UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"))
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                cases = [dict(field.split("=", 1) for field in line.split()[1:])
                         for line in result.stdout.splitlines()]
                self.assertEqual(len(cases), 24)
                mismatches = [c for c in cases if c["actual"] != c["expected"]]
                self.assertEqual(len(mismatches), 15 if version == "original" else 0)
                if version == "fixed":
                    for case in cases:
                        self.assertEqual(int(case["writes"]), int(case["scenario"] not in ("2", "3", "4", "5", "6")))
                print(f"COMPOSER_COMMAND_ERRORS {version} cases={len(cases)} mismatches={len(mismatches)}")

if __name__ == "__main__":
    unittest.main()
