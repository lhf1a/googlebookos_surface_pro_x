"""Unit tests for Turnip hardware-GMEM source verification and workaround revert."""
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT_PATH = ROOT / 'scripts/patch-sq2-turnip-gmem.py'
SPEC = importlib.util.spec_from_file_location('turnip_gmem_policy', SCRIPT_PATH)
GMEM = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GMEM)

MAIN_MESA_TARBALL = Path('/mnt/spx-workspace/chromebookOS/downloads/mesa-26.2.4.tar.xz')
LOCAL_MESA_TARBALL = ROOT / 'downloads/mesa-26.2.4.tar.xz'


def _load_clean_tu_cmd_buffer():
    tarball = LOCAL_MESA_TARBALL if LOCAL_MESA_TARBALL.is_file() else MAIN_MESA_TARBALL
    if tarball.is_file():
        with tarfile.open(tarball) as archive:
            return archive.extractfile('mesa-26.2.4/src/freedreno/vulkan/tu_cmd_buffer.cc').read().decode()
    cached = Path('/root/.cache/sq2-graphics/src/mesa-26.2.4/src/freedreno/vulkan/tu_cmd_buffer.cc')
    if cached.is_file():
        return GMEM._strip_known_workarounds(cached.read_text())
    return None


class TurnipGmemPolicyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.clean_source = _load_clean_tu_cmd_buffer()
        if cls.clean_source is None:
            raise unittest.SkipTest('mesa-26.2.4 tu_cmd_buffer.cc source unavailable')

    def test_clean_hash_matches_upstream_pin(self):
        self.assertEqual(hashlib.sha256(self.clean_source.encode()).hexdigest(), GMEM.ORIGINAL_HASH)

    def test_verify_clean_passes_on_pristine_and_rejects_patched(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            src_root = Path(tmpdir)
            target = src_root / GMEM.TU_CMD_BUFFER_REL
            target.parent.mkdir(parents=True)
            target.write_text(self.clean_source)

            GMEM.verify_clean(src_root)
            self.assertFalse(GMEM.revert_file(src_root))

            self.assertTrue(GMEM.apply_legacy_3dmark_sysmem(src_root))
            with self.assertRaises(ValueError):
                GMEM.verify_clean(src_root)

    def test_revert_strips_legacy_3dmark_and_global_sysmem_idempotently(self):
        legacy_3dmark = self.clean_source.replace(
            GMEM.INCLUDE_TARGET, GMEM.INCLUDE_TARGET + GMEM.INCLUDE, 1
        ).replace(GMEM.ANCHOR, GMEM.LEGACY_3DMARK_BLOCK + GMEM.ANCHOR, 1)
        global_sysmem = self.clean_source.replace(
            GMEM.ANCHOR, GMEM.GLOBAL_SYSMEM_BLOCK + GMEM.ANCHOR, 1
        )
        global_with_include = self.clean_source.replace(
            GMEM.INCLUDE_TARGET, GMEM.INCLUDE_TARGET + GMEM.INCLUDE, 1
        ).replace(GMEM.ANCHOR, GMEM.GLOBAL_SYSMEM_BLOCK + GMEM.ANCHOR, 1)

        for label, variant in (
            ('clean', self.clean_source),
            ('legacy-3dmark', legacy_3dmark),
            ('global-sysmem', global_sysmem),
            ('global-with-include', global_with_include),
        ):
            with self.subTest(variant=label), tempfile.TemporaryDirectory() as tmpdir:
                target = Path(tmpdir) / 'tu_cmd_buffer.cc'
                target.write_text(variant)
                changed = GMEM.revert_file(target)
                self.assertEqual(changed, label != 'clean')
                self.assertEqual(target.read_text(), self.clean_source)
                self.assertFalse(GMEM.revert_file(target))
                GMEM.verify_clean(target)

    def test_apply_legacy_3dmark_is_opt_in_and_idempotent(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            src_root = Path(tmpdir)
            target = src_root / GMEM.TU_CMD_BUFFER_REL
            target.parent.mkdir(parents=True)
            target.write_text(self.clean_source)

            self.assertTrue(GMEM.apply_legacy_3dmark_sysmem(src_root))
            applied = target.read_text()
            self.assertIn('SQ2_A680_1_3DMARK_SYSMEM', applied)
            self.assertIn('com.futuremark.dmandroid.application', applied)
            self.assertFalse(GMEM.apply_legacy_3dmark_sysmem(src_root))
            self.assertEqual(target.read_text(), applied)

            # CLI default reverts cleanly without mutating clean files again.
            res = subprocess.run(
                [sys.executable, str(SCRIPT_PATH), str(src_root)],
                capture_output=True,
                text=True,
                check=True,
            )
            self.assertIn('reverted to pristine hardware-GMEM', res.stdout)
            self.assertEqual(target.read_text(), self.clean_source)
            subprocess.run(
                [sys.executable, str(SCRIPT_PATH), '--verify-clean', str(src_root)],
                capture_output=True,
                text=True,
                check=True,
            )

    def test_drift_is_rejected_without_modification(self):
        drifted_variants = [
            self.clean_source + '// drift\n',
            self.clean_source.replace(
                GMEM.ANCHOR,
                GMEM.LEGACY_3DMARK_BLOCK.replace('return true', 'return false') + GMEM.ANCHOR,
                1,
            ),
            self.clean_source.replace(
                GMEM.ANCHOR,
                GMEM.GLOBAL_SYSMEM_BLOCK.replace('0x06080001', '0x06080002') + GMEM.ANCHOR,
                1,
            ),
        ]
        for idx, body in enumerate(drifted_variants):
            with self.subTest(idx=idx), tempfile.TemporaryDirectory() as tmpdir:
                target = Path(tmpdir) / 'tu_cmd_buffer.cc'
                target.write_text(body)
                with self.assertRaises(ValueError):
                    GMEM.revert_file(target)
                with self.assertRaises(ValueError):
                    GMEM.verify_clean(target)
                with self.assertRaises(ValueError):
                    GMEM.apply_legacy_3dmark_sysmem(target)
                self.assertEqual(target.read_text(), body)


if __name__ == '__main__':
    unittest.main()
