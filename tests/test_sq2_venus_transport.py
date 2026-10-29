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
            self.assertIn('!venus_sq2_uc_valid(desc.da, desc.size)', hfi)
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
