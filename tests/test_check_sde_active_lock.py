"""Host-independent extraction/parameter checks for the active-list regression."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("active_lock_checker", ROOT / "scripts/check-sde-active-lock.py")
CHECKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKER)


class ActiveLockExtractionTests(unittest.TestCase):
    def test_actual_function_is_not_rewritten(self):
        body = "int example(int value)\n{\n\treturn value + 1;\n}"
        self.assertEqual(CHECKER.extract("/* context */\n" + body + "\n", "example"), body)

    def test_ambiguous_function_rejected(self):
        body = "int example(void)\n{\nreturn 0;\n}\n"
        with self.assertRaises(ValueError):
            CHECKER.extract(body * 2, "example")

    def test_missing_function_rejected(self):
        with self.assertRaises(ValueError):
            CHECKER.extract("int other(void);\n", "example")

    def test_multiline_uapi_define_preserved(self):
        definition = "#define FLAGS (1 | " + chr(92) + "\n    2)"
        self.assertEqual(CHECKER.extract_define(definition + "\n", "FLAGS"), definition)

    def test_duplicate_uapi_define_rejected(self):
        with self.assertRaises(ValueError):
            CHECKER.extract_define("#define FLAGS 1\n#define FLAGS 2\n", "FLAGS")

    def test_truncated_uapi_define_rejected(self):
        with self.assertRaises(ValueError):
            CHECKER.extract_define("#define FLAGS (1 | " + chr(92) + "\n", "FLAGS")

    def test_source_root_and_msm_directory_parameters(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            msm = root / "techpack/display/msm"
            msm.mkdir(parents=True)
            for name in ("msm_gem.c", "msm_gem_vma.c", "msm_gem_prime.c", "msm_gem.h", "msm_drv.h"):
                (msm / name).write_text("fixture\n")
            uapi = root / "include/uapi/drm/msm_drm.h"
            uapi.parent.mkdir(parents=True)
            uapi.write_text("#define FLAGS 1\n")
            self.assertEqual(CHECKER.source_paths(root), (msm.resolve(), uapi.resolve()))
            self.assertEqual(CHECKER.source_paths(msm), (msm.resolve(), uapi.resolve()))

    def test_missing_uapi_requires_explicit_parameter(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            msm = root / "techpack/display/msm"
            msm.mkdir(parents=True)
            for name in ("msm_gem.c", "msm_gem_vma.c", "msm_gem_prime.c", "msm_gem.h", "msm_drv.h"):
                (msm / name).write_text("fixture\n")
            with self.assertRaises(ValueError):
                CHECKER.source_paths(root)
            uapi = root / "explicit-uapi.h"
            uapi.write_text("#define FLAGS 1\n")
            self.assertEqual(CHECKER.source_paths(root, uapi), (msm.resolve(), uapi.resolve()))

    def test_archived_tools_have_no_private_input_paths(self):
        for path in (ROOT / "scripts/check-sde-active-lock.py", ROOT / "tests/sde_active_lock_test.c"):
            text = path.read_text()
            for private in ("/mnt/spx-workspace", "/mnt/c/Users", "out/kgsl-resume"):
                self.assertNotIn(private, text)

    def test_timeout_output_accepts_text_or_bytes(self):
        self.assertEqual(CHECKER.timeout_text(b"timeout"), "timeout")
        self.assertEqual(CHECKER.timeout_text("timeout"), "timeout")
        self.assertEqual(CHECKER.timeout_text(None), "")


if __name__ == "__main__":
    unittest.main()
