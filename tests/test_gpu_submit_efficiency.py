import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("gpu_submit_check", ROOT / "scripts/check-gpu-submit-efficiency.py")
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)


class SavedGpuSubmitTests(unittest.TestCase):
    def test_manifest_artifact_bytes(self):
        config = json.loads(CHECK.CONFIG.read_text())
        self.assertFalse(config["default_build_enabled"])
        for key in ("patch", "incremental_patch"):
            CHECK.checked(ROOT / config[key], config[key + "_sha256"])

    def test_complete_patch_has_exact_five_source_paths(self):
        config = json.loads(CHECK.CONFIG.read_text())
        patch = (ROOT / config["patch"]).read_text()
        paths = [line[6:] for line in patch.splitlines() if line.startswith("+++ b/")]
        self.assertEqual(paths, [e["path"] for e in config["files"]])
        self.assertTrue(all(not Path(p).is_absolute() and ".." not in Path(p).parts for p in paths))

    def test_incremental_patch_only_changes_submit(self):
        config = json.loads(CHECK.CONFIG.read_text())
        patch = (ROOT / config["incremental_patch"]).read_text()
        self.assertEqual([s[6:] for s in patch.splitlines() if s.startswith("+++ b/")],
                         ["drivers/gpu/drm/msm/msm_gem_submit.c"])
        changed = [e["path"] for e in config["files"] if e["v4_sha256"] != e["v5_sha256"]]
        self.assertEqual(changed, ["drivers/gpu/drm/msm/msm_gem_submit.c"])

    def test_checked_is_read_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "source"
            p.write_bytes(b"original\n")
            expected = hashlib.sha256(p.read_bytes()).hexdigest()
            self.assertEqual(CHECK.checked(p, expected), b"original\n")
            with self.assertRaises(ValueError):
                CHECK.checked(p, "0" * 64)
            self.assertEqual(p.read_bytes(), b"original\n")

    def test_existing_output_refused_without_changes(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "already-there"
            output.mkdir()
            marker = output / "keep"
            marker.write_bytes(b"untouched")
            with self.assertRaises(FileExistsError):
                CHECK.prepare(Path(tmp) / "missing-source", output)
            self.assertEqual(list(output.iterdir()), [marker])
            self.assertEqual(marker.read_bytes(), b"untouched")

    def test_missing_input_does_not_create_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "new"
            with self.assertRaises(FileNotFoundError):
                CHECK.prepare(Path(tmp) / "missing-source", output)
            self.assertFalse(output.exists())

    def test_wrong_input_does_not_create_output_or_change_source(self):
        with tempfile.TemporaryDirectory() as tmp:
            kernel = Path(tmp) / "kernel"
            config = json.loads(CHECK.CONFIG.read_text())
            p = kernel / config["files"][0]["path"]
            p.parent.mkdir(parents=True)
            p.write_bytes(b"different source")
            output = Path(tmp) / "output"
            with self.assertRaises(ValueError):
                CHECK.prepare(kernel, output)
            self.assertFalse(output.exists())
            self.assertEqual(p.read_bytes(), b"different source")

    def test_fixture_python_syntax(self):
        files = sorted((ROOT / "tests/fixtures/gpu-submit-efficiency-v5").glob("*.py"))
        self.assertEqual(len(files), 5)
        for p in files:
            compile(p.read_bytes(), str(p), "exec")


if __name__ == "__main__":
    unittest.main()
