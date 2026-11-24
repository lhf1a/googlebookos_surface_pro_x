"""Verification suite for SQ2 Venus idle runtime PM and unused UFS GDSC collapse."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import full_image_video as video

EXTRACTED_VENUS = Path(os.environ['SPX_EXTRACTED_VENUS']) if os.environ.get('SPX_EXTRACTED_VENUS') else None
UFS_GDSC_PATCH = ROOT / 'patches/spx-portable-boot/0018-clk-qcom-gcc-sc8180x-power-off-unused-ufs-gdscs.patch'


class VenusRuntimePMTests(unittest.TestCase):
    def test_runtime_pm_state_machine_and_lock_order_asan(self):
        with tempfile.TemporaryDirectory() as td:
            inc = Path(td) / 'inc'
            inc.mkdir()
            (inc / 'hfi_venus.h').write_text('/* stub */\n', encoding='utf-8')
            out = Path(td) / 'rtpm_test'
            cmd = [
                'cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                '-I', str(inc),
                str(ROOT / 'tests/sq2_venus_runtime_pm_test.c'),
                '-o', str(out),
            ]
            res = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, res.stderr)
            run = subprocess.run([str(out)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('SQ2_VENUS_RUNTIME_PM_PASS', run.stdout)

    def test_patch_runtime_pm_and_ensure_hooks_idempotent_on_real_venus_copy(self):
        if EXTRACTED_VENUS is None or not EXTRACTED_VENUS.is_dir():
            self.skipTest('requires extracted external/venus build tree')
        with tempfile.TemporaryDirectory() as td:
            work = Path(td) / 'venus'
            shutil.copytree(EXTRACTED_VENUS, work)
            first = video.ensure_venus_source_hooks(work)
            self.assertTrue(first['updated_headers'])
            core_h = (work / 'core.h').read_text(encoding='utf-8')
            core_c = (work / 'core.c').read_text(encoding='utf-8')
            hfi_c = (work / 'hfi.c').read_text(encoding='utf-8')
            self.assertIn('struct mutex sq2_pm_mutex;', core_h)
            self.assertIn('return venus_sq2_runtime_suspend(dev);', core_c)
            self.assertIn('return venus_sq2_runtime_resume(dev);', core_c)
            self.assertIn('pm_runtime_disable(core->dev);', core_c)
            self.assertIn('#include <linux/pm_runtime.h>', hfi_c)
            self.assertIn('pm_runtime_resume_and_get(core->dev)', hfi_c)
            self.assertIn('pm_runtime_put_autosuspend(core->dev)', hfi_c)
            self.assertIn('venus_sq2_pm_publish(core);', (work / 'venus-sq2-core.h').read_text(encoding='utf-8'))

            snap = {p.name: p.read_bytes() for p in work.iterdir() if p.is_file()}
            second = video.ensure_venus_source_hooks(work)
            self.assertEqual(second['updated_headers'], [])
            self.assertEqual(snap, {p.name: p.read_bytes() for p in work.iterdir() if p.is_file()})


class UnusedUfsGdscPatchTests(unittest.TestCase):
    def test_patch_is_portable_and_checks_dt_availability(self):
        self.assertTrue(UFS_GDSC_PATCH.is_file())
        text = UFS_GDSC_PATCH.read_text(encoding='utf-8')
        self.assertIn('--- a/drivers/clk/qcom/gcc-sc8180x.c', text)
        self.assertIn('for_each_compatible_node(np, NULL, "qcom,sc8180x-ufshc")', text)
        self.assertIn('of_device_is_available(np)', text)
        self.assertIn('&ufs_phy_gdsc', text)
        self.assertIn('&ufs_card_2_gdsc', text)
        self.assertIn('regmap_read_poll_timeout', text)
        self.assertNotIn('hlos1_vote_mmnoc_mmu_tbu', text)
        self.assertIsNone(re.search(r'/home/[A-Za-z0-9._-]+/', text))


if __name__ == '__main__':
    unittest.main()
