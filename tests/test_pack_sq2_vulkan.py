"""Regression for missing directory entries in the Vulkan initramfs delta."""
import gzip
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/pack-sq2-vulkan.py'
spec = importlib.util.spec_from_file_location('pack_vulkan', SCRIPT)
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)

class VulkanArchiveTests(unittest.TestCase):
    def test_missing_permissions_directory_is_emitted_before_children(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            dirs = ['etc', 'etc/googlebook-security', 'etc/googlebook-security/etc',
                    'etc/googlebook-security/lib64', 'etc/googlebook-security/lib64/hw']
            archive = b''.join(pack.entry(n, b'', 0o40755) for n in dirs)
            prop = b'ro.hardware.vulkan=none\nro.hardware.egl=freedreno\ndebug.hwui.renderer=skiagl\n'
            archive += pack.entry('etc/googlebook-security/build.prop', prop, 0o100644)
            archive += pack.entry('TRAILER!!!', b'', 0)
            base = gzip.compress(archive)
            (root/'base.gz').write_bytes(base)
            (root/'driver.so').write_bytes(b'\x7fELF\x02\x01'+b'\0'*12+b'\xb7\0')
            subprocess.run([sys.executable, str(SCRIPT), str(root/'base.gz'),
                            str(root/'driver.so'), str(root/'out.gz'),
                            '--expected-base-sha256', hashlib.sha256(base).hexdigest()],
                           check=True, capture_output=True)
            files = pack.entries(gzip.decompress((root/'out.gz').read_bytes()))
            permissions = 'etc/googlebook-security/etc/permissions'
            self.assertEqual(files[permissions], (0o40755, b''))
            ordered = list(files)
            for name in files:
                if name.startswith(permissions+'/'):
                    self.assertLess(ordered.index(permissions), ordered.index(name))
            self.assertIn(b'4206592', files[permissions+'/android.hardware.vulkan.version-1_3.xml'][1])

if __name__ == '__main__':
    unittest.main()
