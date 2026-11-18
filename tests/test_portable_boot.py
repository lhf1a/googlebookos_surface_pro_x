"""Regression for the early Android userspace prerequisites."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'scripts'))
import disk_image


class PortableBoot(unittest.TestCase):
    @unittest.skipUnless(shutil.which('patchelf') and shutil.which('cc'), 'ELF fixture requires patchelf and cc')
    def test_bootstrap_hidl_rewrite_keeps_both_consumers_and_is_idempotent(self):
        with tempfile.TemporaryDirectory() as td:
            elf = Path(td)/'manager'
            subprocess.run(['cc','-x','c','-Wl,--dynamic-linker=/system/bin/linker64','-o',elf,'-'],
                           input='int main(void) { return 0; }\n', text=True, check=True)
            paths = ('etc/sq2-hidl/hwservicemanager',
                     'etc/sq2-qti-system/hidl-support/hwservicemanager')
            original = dict.fromkeys(paths, (0o100755, elf.read_bytes()))
            updated = disk_image.early_hidl_bootstrap(original)
            self.assertEqual(set(updated), set(paths))
            self.assertEqual(updated[paths[0]], updated[paths[1]])
            elf.write_bytes(updated[paths[0]][1])
            self.assertEqual(subprocess.check_output(['patchelf','--print-interpreter',elf],text=True).strip(),
                             '/system/bin/bootstrap/linker64')
            self.assertEqual(disk_image.early_hidl_bootstrap(updated), updated)
            self.assertEqual(disk_image.early_hidl_bootstrap({}), {})
            with self.assertRaisesRegex(ValueError, 'Conflicting'):
                disk_image.early_hidl_bootstrap({paths[0]: original[paths[0]], paths[1]: updated[paths[1]]})

    def test_no_tpm_removes_only_real_hardware_dependencies(self):
        script = (ROOT/'probe/test2/gscd-availability-stage').read_text()
        for has_tpm in (False, True):
            with self.subTest(has_tpm=has_tpm), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                init = root/'system/etc/init'; init.mkdir(parents=True)
                (root/'debug_ramdisk').mkdir()
                (root/'version').write_text('2\n')
                manager = ('service android.system.desktop.security.gscd /system/bin/gscd\n'
                           '    class early_hal\n    user hsm\n    group hsm\n')
                util = 'on post-fs\n    wait /dev/tpm0 10\n    exec -- /system/bin/gscutil\n'
                clear = 'on early-fs\n    mkdir /metadata/hwsec_metadata 775 root root\n\non post-fs-data\n    exec_background -- /system/bin/check_tpm_clear\n'
                for name, data in [('gscd',manager),('gscutil',util),('check_tpm_clear',clear)]:
                    (init/(name+'.rc')).write_text(data)
                s = script.replace('/newroot',td).replace('/dev/tpm0','/dev/null' if has_tpm else str(root/'missing'))
                s = s.replace('/sys/class/tpm/tpm0/tpm_version_major',str(root/'version'))
                result = subprocess.run(['sh'],input='mount() { cp "$2" "$3"; }\n'+s,
                                        capture_output=True,text=True,check=True)
                self.assertEqual('    disabled\n' in (init/'gscd.rc').read_text(), not has_tpm)
                if has_tpm:
                    self.assertEqual((init/'gscutil.rc').read_text(),util)
                    self.assertEqual((init/'check_tpm_clear.rc').read_text(),clear)
                else:
                    self.assertIn('GSC_BACKEND_UNAVAILABLE_SKIP',result.stdout)
                    self.assertNotIn('wait ',(init/'gscutil.rc').read_text())
                    self.assertNotIn('exec_background ',(init/'check_tpm_clear.rc').read_text())
                    self.assertIn('mkdir /metadata/hwsec_metadata',(init/'check_tpm_clear.rc').read_text())
