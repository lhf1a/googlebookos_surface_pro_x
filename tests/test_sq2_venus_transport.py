"""Exercise the actual pinned-source preparer; no device access."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
KERNEL = Path('/root/.cache/sq2-thermal/kernel')


@unittest.skipUnless(KERNEL.exists(), 'pinned WSL kernel tree required')
class VenusTransport(unittest.TestCase):
    def test_lab_resources_and_buffer_checks(self):
        with tempfile.TemporaryDirectory() as tmp:
            dest = Path(tmp) / 'resources'
            cmd = [sys.executable, str(ROOT / 'scripts/prepare-sq2-venus-iris1.py'),
                   '--output', str(dest), '--sq2-resources']
            rejected = subprocess.run(cmd, capture_output=True, text=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertFalse(dest.exists())
            subprocess.run(cmd + ['--firmware-lease', '--platform-entry'], check=True)
            core = (dest / 'core.c').read_text()
            helpers = (dest / 'helpers.c').read_text()
            res = (dest / 'venus-sq2-resources.h').read_text()
            self.assertIn('"microsoft,sq2-venus-lab", .data = &sq2_lab_res', core)
            self.assertIn('.cp_size = 0x25800000', res)
            self.assertNotIn('0x70800000', res)
            self.assertEqual(helpers.count('!sq2_venus_buffer_valid('), 4)
            self.assertIn('!sq2_venus_payload_valid(', helpers)
            section = helpers[helpers.index('int venus_helper_vb2_buf_init('):]
            self.assertLess(section.index('!sq2_venus_buffer_valid('), section.index('buf->dma_addr ='))

    def test_independent_core_dispatch(self):
        with tempfile.TemporaryDirectory() as tmp:
            dest = Path(tmp) / 'entry'
            cmd = [sys.executable, str(ROOT / 'scripts/prepare-sq2-venus-iris1.py'),
                   '--output', str(dest), '--platform-entry']
            rejected = subprocess.run(cmd, capture_output=True, text=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertFalse(dest.exists())
            subprocess.run(cmd + ['--firmware-lease'], check=True)
            core = (dest / 'core.c').read_text()
            entry = (dest / 'venus-sq2-core.h').read_text()
            self.assertLess(core.index('return venus_sq2_probe(pdev, res);'),
                            core.index('core = devm_kzalloc'))
            self.assertIn('return venus_sq2_remove(pdev);', core)
            self.assertIn('venus_sq2_stop_core(core);', core)
            self.assertIn('if (!sq2_platform_allow) return -EOPNOTSUPP;', entry)
            self.assertNotIn('.sq2_windows_hfi =', core)
            self.assertEqual(entry.count('ret = venus_boot(core);'), 1)
            worker = entry[entry.index('static void venus_sq2_error'):entry.index('static int venus_sq2_remove')]
            self.assertNotIn('ret = venus_boot', worker)
            self.assertNotIn('schedule_delayed_work(', worker)
            self.assertLess(entry.index('ret = hfi_core_init(core);'),
                            entry.index('ret = of_platform_populate'))
            self.assertIn('if (stop || core->sq2_public)', entry)
            self.assertIn('false : venus_fw_low_power_mode', (dest / 'hfi_venus.c').read_text())

    def test_pas_dispatch_is_opt_in_and_probe_is_blocked(self):
        with tempfile.TemporaryDirectory() as tmp:
            for lease in (False, True):
                dest = Path(tmp) / str(lease)
                cmd = [sys.executable, str(ROOT / 'scripts/prepare-sq2-venus-iris1.py'),
                       '--output', str(dest)]
                if lease:
                    cmd.append('--firmware-lease')
                subprocess.run(cmd, check=True)
                fw = (dest / 'firmware.c').read_text()
                core = (dest / 'core.c').read_text()
                self.assertEqual('return venus_sq2_boot(core);' in fw, lease)
                self.assertEqual('return venus_sq2_shutdown(core);' in fw, lease)
                self.assertIn('if (IS_SQ2_HFI(core))\n\t\treturn -EOPNOTSUPP;', core)
                self.assertNotIn('.sq2_windows_hfi =', core)
                if lease:
                    self.assertLess(fw.index('return venus_sq2_boot(core);'),
                                    fw.index('ret = venus_load_fw(core,'))
                    self.assertIn('core->sq2_boot_attempted = true;',
                                  (dest / 'venus-sq2-firmware.h').read_text())

    def test_generated_transport_and_legacy_isolation(self):
        with tempfile.TemporaryDirectory() as tmp:
            dest = Path(tmp) / 'venus'
            subprocess.run([sys.executable, str(ROOT / 'scripts/prepare-sq2-venus-iris1.py'),
                            '--kernel', str(KERNEL), '--output', str(dest)], check=True)
            hfi = (dest / 'hfi_venus.c').read_text()
            self.assertIn('venus_sq2_cpu_map(hdev->core,', hfi)
            self.assertIn('desc.da += ALIGNED_QUEUE_SIZE;', hfi)
            self.assertIn('desc.kva += ALIGNED_QUEUE_SIZE;', hfi)
            self.assertIn('if (!IS_SQ2_HFI(hdev->core))\n\t\tvenus_free(hdev, &hdev->sfr);', hfi)
            self.assertIn('if (IS_IRIS1(hdev->core) && !IS_SQ2_HFI(hdev->core))', hfi)
            self.assertIn('if (!IS_SQ2_HFI(hdev->core))\n\t\twritel(1, cpu_cs_base + CPU_CS_SCIACMDARG3);', hfi)
            self.assertIn('sq2_venus_dma_check_uc(hdev->core->dev, desc.da, desc.size)', hfi)
            self.assertIn('sq2_venus_dma_begin(core->dev, &hdev->sq2_dma)', hfi)
            self.assertEqual(hfi.count('sq2_venus_dma_restore(core->dev, &hdev->sq2_dma);'), 2)
            self.assertLess(hfi.index('venus_interface_queues_release(hdev);'),
                            hfi.index('sq2_venus_dma_restore(core->dev, &hdev->sq2_dma);'))
            self.assertNotIn('.sq2_windows_hfi =', (dest / 'core.c').read_text())
            manifest = json.loads((dest / 'candidate.json').read_text())
            self.assertIn('no-platform-match', manifest['status'])
            # Refuse reuse, rather than overwrite a prior candidate.
            again = subprocess.run([sys.executable, str(ROOT / 'scripts/prepare-sq2-venus-iris1.py'),
                                    '--output', str(dest)], capture_output=True, text=True)
            self.assertNotEqual(again.returncode, 0)
            self.assertIn('output exists', again.stderr)


if __name__ == '__main__':
    unittest.main()
