"""Retired C2 removal must preserve unrelated archive metadata and payloads."""
import gzip
import importlib.util
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('hidlpack',ROOT/'scripts/pack-quest2-hidl.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class HidlPack(unittest.TestCase):
 def test_removes_only_project_c2_records(self):
  entry=m.pack.cpio.entry
  keep=bytearray(entry('etc/sq2-hidl/lib64/libc2dcolorconvert.so',b'color',0o100644))
  keep[22:30]=b'000003e8'  # Retain non-root uid and all other header fields.
  keep=bytes(keep)
  tail=entry('TRAILER!!!',b'',0)
  deleted=entry('etc/sq2-c2-runtime-boot/runtime.tar',b'old',0o100644)
  base=gzip.compress(deleted+keep+tail)+gzip.compress(entry('bin/video-codec-manifest-stage',b'old',0o100755)+tail)
  result,removed=m.strip_c2(base)
  self.assertEqual(removed,{'etc/sq2-c2-runtime-boot/runtime.tar','bin/video-codec-manifest-stage'})
  self.assertEqual(gzip.decompress(result),keep+tail+tail)
 def test_rejects_truncated_record(self):
  with self.assertRaises(ValueError):m.strip_c2(gzip.compress(m.pack.cpio.entry('test',b'payload',0o100644)[:-5]))
if __name__=='__main__':unittest.main()
