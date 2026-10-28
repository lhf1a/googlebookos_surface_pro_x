"""Opt-in integration tests with private lab artifacts; run inside WSL.

SQ2_VULKAN_BOOT_TEST_IMAGE supplies the built, credential-bearing image. Only
the helper, vendor GPU files and candidate are extracted into a temporary tree.
"""
import gzip
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
IMAGE=os.environ.get('SQ2_VULKAN_BOOT_TEST_IMAGE')

@unittest.skipUnless(IMAGE,'private boot candidate not supplied')
class BootOverlay(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  spec=importlib.util.spec_from_file_location('cpio',ROOT/'scripts/pack-sq2-vulkan.py')
  mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
  cls.entries=mod.entries(gzip.decompress(Path(IMAGE).read_bytes()))

 def setUp(self):
  self.temp=tempfile.TemporaryDirectory(prefix='sq2-vulkan-boot-')
  self.addCleanup(self.temp.cleanup)
  self.root=Path(self.temp.name)
  self.vendor=self.root/'newroot/vendor'
  for name,(mode,body) in self.entries.items():
   if mode&0o170000!=0o100000:continue
   if name.startswith('etc/sq2-vulkan-closed/'):
    path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(body)
   elif name.startswith('etc/googlebook-security/lib64/'):
    path=self.vendor/name.removeprefix('etc/googlebook-security/')
    path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(body)
  for name in ('etc/init','etc/permissions'):(self.vendor/name).mkdir(parents=True)
  (self.root/'proc').mkdir()
  (self.root/'proc/mounts').write_text('overlay /newroot/vendor overlay rw 0 0\n')
  self.script=self.entries['bin/closed-vulkan-start'][1].decode()
  # Rewrite only paths, not helper logic. No host /proc, /etc or mounts changed.
  self.script=self.script.replace('/proc/cmdline',str(self.root/'proc/cmdline'))
  self.script=self.script.replace('/proc/mounts',str(self.root/'proc/mounts'))
  self.script=self.script.replace('source=/etc/sq2-vulkan-closed','source='+str(self.root/'etc/sq2-vulkan-closed'))
  self.script=self.script.replace('vendor=/newroot/vendor','vendor='+str(self.vendor))

 def run_helper(self,cmdline):
  (self.root/'proc/cmdline').write_text(cmdline+'\n')
  return subprocess.run(['sh','-s'],input=self.script,text=True,capture_output=True)

 def snapshot(self):
  return {p.relative_to(self.vendor).as_posix():p.read_bytes() for p in self.vendor.rglob('*') if p.is_file()}

 def test_disabled_preserves_original_vendor(self):
  before=self.snapshot()
  for option in ('sq2.vulkan=mesa','sq2.vulkan=0','sq2.rescue=1'):
   result=self.run_helper('suspend.screen_only=1 '+option+' nohibernate')
   self.assertEqual(result.returncode,0,result.stderr)
   self.assertEqual(before,self.snapshot())

 def test_corrupt_bundle_rejected_before_mutation(self):
  before=self.snapshot()
  (self.root/'etc/sq2-vulkan-closed/libgsl.so').write_bytes(b'corrupt')
  self.assertNotEqual(self.run_helper('sq2.vulkan=closed').returncode,0)
  self.assertEqual(before,self.snapshot())

 def test_enabled_preserves_egl_and_installs_matching_hal(self):
  before=self.snapshot()
  result=self.run_helper('suspend.screen_only=1 nohibernate sq2.vulkan=closed')
  self.assertEqual(result.returncode,0,result.stdout+result.stderr)
  after=self.snapshot()
  for name,body in before.items():
   if name!='lib64/hw/vulkan.freedreno.so':self.assertEqual(body,after[name],name)
  self.assertEqual(after['lib64/hw/vulkan.freedreno.so'],after['lib64/egl/libquest2-vulkan-wrapper.so'])
  self.assertIn(b'4198400',after['etc/permissions/android.hardware.vulkan.version-1_3.xml'])
  self.assertIn(b'on post-fs-data',after['etc/init/sq2-closed-vulkan.rc'])
  self.assertEqual(after['etc/sq2-vulkan-closed/qgl_config.txt'],self.entries['etc/sq2-vulkan-closed/qgl_config.txt'][1])

if __name__=='__main__':unittest.main()
