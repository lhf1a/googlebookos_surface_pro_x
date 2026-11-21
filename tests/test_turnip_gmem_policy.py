"""Unit tests for Turnip hardware-GMEM source verification and workaround revert."""
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SCRIPT_PATH = ROOT / 'scripts/patch-sq2-turnip-gmem.py'
SPEC = importlib.util.spec_from_file_location('turnip_gmem_policy', SCRIPT_PATH)
GMEM = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GMEM)

MAIN_MESA_TARBALL = Path('/mnt/spx-workspace/chromebookOS/downloads/mesa-26.2.4.tar.xz')
LOCAL_MESA_TARBALL = ROOT / 'downloads/mesa-26.2.4.tar.xz'
SYNTHETIC_CLEAN_SOURCE = (
    '#include "tu_cmd_buffer.h"\n'
    'static bool use_sysmem_rendering(void) {\n'
    '   /* can\'t fit attachments into gmem */\n'
    '   return false;\n'
    '}\n'
)


def _load_clean_tu_cmd_buffer():
    try:
        tarball = LOCAL_MESA_TARBALL if LOCAL_MESA_TARBALL.is_file() else MAIN_MESA_TARBALL
        if tarball.is_file():
            with tarfile.open(tarball) as archive:
                return archive.extractfile('mesa-26.2.4/src/freedreno/vulkan/tu_cmd_buffer.cc').read().decode()
        cached = Path('/root/.cache/sq2-graphics/src/mesa-26.2.4/src/freedreno/vulkan/tu_cmd_buffer.cc')
        if cached.is_file():
            return GMEM._strip_known_workarounds(cached.read_text())
    except OSError:
        return None
    return None


class TurnipGmemPolicyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.upstream_source = _load_clean_tu_cmd_buffer()
        cls.clean_source = cls.upstream_source or SYNTHETIC_CLEAN_SOURCE
        cls.clean_hash = hashlib.sha256(cls.clean_source.encode()).hexdigest()

    def test_clean_hash_matches_upstream_pin(self):
        if self.upstream_source is None:
            self.skipTest('mesa-26.2.4 tu_cmd_buffer.cc source unavailable')
        self.assertEqual(hashlib.sha256(self.upstream_source.encode()).hexdigest(), GMEM.ORIGINAL_HASH)

    def test_apply_legacy_patch_is_completely_removed(self):
        self.assertFalse(hasattr(GMEM, 'apply_legacy_3dmark_sysmem'))
        self.assertFalse(hasattr(GMEM, 'LEGACY_3DMARK_BLOCK'))
        self.assertFalse(hasattr(GMEM, 'GLOBAL_SYSMEM_BLOCK'))

        with tempfile.TemporaryDirectory() as tmpdir:
            src_root = Path(tmpdir)
            target = src_root / GMEM.TU_CMD_BUFFER_REL
            target.parent.mkdir(parents=True)
            target.write_text(self.clean_source)

            res = subprocess.run(
                [sys.executable, str(SCRIPT_PATH), '--apply-legacy-3dmark-sysmem', str(src_root)],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(res.returncode, 0)
            self.assertEqual(target.read_text(), self.clean_source)

    def test_verify_clean_passes_on_pristine_and_rejects_patched(self):
        with mock.patch.object(GMEM, 'ORIGINAL_HASH', self.clean_hash), tempfile.TemporaryDirectory() as tmpdir:
            src_root = Path(tmpdir)
            target = src_root / GMEM.TU_CMD_BUFFER_REL
            target.parent.mkdir(parents=True)
            target.write_text(self.clean_source)

            GMEM.verify_clean(src_root)
            self.assertFalse(GMEM.revert_file(src_root))

            synthetic_legacy = self.clean_source.replace(
                GMEM.ANCHOR,
                '   /* SQ2_A680_1_3DMARK_SYSMEM: legacy */\n   if (0) {\n      return true;\n   }\n\n' + GMEM.ANCHOR,
                1,
            )
            target.write_text(synthetic_legacy)
            with self.assertRaises(ValueError):
                GMEM.verify_clean(src_root)

    def test_revert_strips_legacy_3dmark_and_global_sysmem_idempotently(self):
        legacy_3dmark_block = '   /* SQ2_A680_1_3DMARK_SYSMEM: legacy */\n   if (0) {\n      return true;\n   }\n\n'
        global_sysmem_block = '   /* SQ2_A680_1_GLOBAL_SYSMEM: legacy */\n   if (0) {\n      return true;\n   }\n\n'
        legacy_3dmark = self.clean_source.replace(
            GMEM.INCLUDE_TARGET, GMEM.INCLUDE_TARGET + GMEM.INCLUDE, 1
        ).replace(GMEM.ANCHOR, legacy_3dmark_block + GMEM.ANCHOR, 1)
        global_sysmem = self.clean_source.replace(
            GMEM.ANCHOR, global_sysmem_block + GMEM.ANCHOR, 1
        )
        global_with_include = self.clean_source.replace(
            GMEM.INCLUDE_TARGET, GMEM.INCLUDE_TARGET + GMEM.INCLUDE, 1
        ).replace(GMEM.ANCHOR, global_sysmem_block + GMEM.ANCHOR, 1)

        with mock.patch.object(GMEM, 'ORIGINAL_HASH', self.clean_hash):
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

    def test_cli_default_reverts_and_verifies_clean(self):
        if self.upstream_source is None:
            self.skipTest('mesa-26.2.4 tu_cmd_buffer.cc source unavailable')
        with tempfile.TemporaryDirectory() as tmpdir:
            src_root = Path(tmpdir)
            target = src_root / GMEM.TU_CMD_BUFFER_REL
            target.parent.mkdir(parents=True)
            target.write_text(self.upstream_source)

            res = subprocess.run(
                [sys.executable, str(SCRIPT_PATH), str(src_root)],
                capture_output=True,
                text=True,
                check=True,
            )
            self.assertIn('already pristine hardware-GMEM', res.stdout)
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
                '   /* SQ2_A680_1_3DMARK_SYSMEM: drift */\n   if (0) {\n      return false;\n   }\n\n' + GMEM.ANCHOR,
                1,
            ),
            self.clean_source.replace(
                GMEM.ANCHOR,
                '   /* SQ2_A680_1_GLOBAL_SYSMEM: drift */\n   if (0) {\n      return false;\n   }\n\n' + GMEM.ANCHOR,
                1,
            ),
        ]
        with mock.patch.object(GMEM, 'ORIGINAL_HASH', self.clean_hash):
            for idx, body in enumerate(drifted_variants):
                with self.subTest(idx=idx), tempfile.TemporaryDirectory() as tmpdir:
                    target = Path(tmpdir) / 'tu_cmd_buffer.cc'
                    target.write_text(body)
                    with self.assertRaises(ValueError):
                        GMEM.revert_file(target)
                    with self.assertRaises(ValueError):
                        GMEM.verify_clean(target)
                    self.assertEqual(target.read_text(), body)


if __name__ == '__main__':
    unittest.main()
