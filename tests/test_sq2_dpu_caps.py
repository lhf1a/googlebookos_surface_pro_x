"""Verify the real emitter and reject unknown source before any source mutation."""
import importlib.util
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "out/quest2-video-618-20261107/uncached-native/source/external/edp-618/drivers/gpu/drm/msm/disp/dpu1"
spec = importlib.util.spec_from_file_location("dpu_caps_patch", ROOT / "scripts/patch-sq2-dpu-caps.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class DpuCaps(unittest.TestCase):
    @unittest.skipUnless(SOURCE.is_dir(), "requires pinned kernel catalog")
    def test_actual_emitter_allocation_failures_and_real_fields(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            header = p / "dpu_caps.h"
            text = (ROOT / "probe/graphics/dpu-caps.h").read_text()
            header.write_text("\n".join(line for line in text.splitlines()
                                       if not line.startswith("#include")) + "\n")
            macros = re.findall(r"^#define SSPP_SCALER_VER\([^\n]+$",
                                (SOURCE / "dpu_hw_catalog.c").read_text(), re.MULTILINE)
            self.assertEqual(len(macros), 1)
            catalog_header = p / "catalog-version.h"
            catalog_header.write_text(macros[0] + "\n")
            binary = p / "caps"
            subprocess.run(["gcc", "-include", str(catalog_header), "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-parameter", "-fsanitize=address,undefined",
                            "-fno-pie", "-no-pie", '-DDPU_CAPS_HEADER="' + str(header) + '"',
                            str(ROOT / "tests/dpu_caps_emit_test.c"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    @unittest.skipUnless(SOURCE.is_dir(), "requires pinned kernel source")
    def test_patch_preserves_formats_and_rejects_unknown_without_writes(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            before = {}
            for name in module.SOURCE_HASHES:
                before[name] = (SOURCE / name).read_bytes()
                (p / name).write_bytes(before[name])
            (p / "dpu_kms.c").write_bytes(b"unknown")
            with self.assertRaises(ValueError):
                module.patch(p)
            self.assertEqual((p / "dpu_plane.c").read_bytes(), before["dpu_plane.c"])
            self.assertFalse((p / "dpu_caps.h").exists())
            (p / "dpu_kms.c").write_bytes(before["dpu_kms.c"])
            result = module.patch(p)
            self.assertEqual(len(result), 5)
            plane = (p / "dpu_plane.c").read_text()
            self.assertIn("SQ2_NO_UBWC_MODIFIER", plane)
            start = "static const uint64_t supported_format_modifiers[]"
            old = before["dpu_plane.c"].decode()
            if start in old:
                original = old[old.index(start):old.index("};", old.index(start)) + 2]
                self.assertIn(original, plane)
            self.assertIn("dpu_kms->core_rev = core_rev;", (p / "dpu_kms.c").read_text())

if __name__ == "__main__":
    unittest.main()
