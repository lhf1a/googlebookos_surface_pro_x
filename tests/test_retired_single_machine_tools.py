"""Retired device-bound diagnostics must not return to executable build paths."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
RETIRED=['probe/test2/lte-fs12-seed', 'probe/test2/lte-fsg-seed', 'probe/virtualization/el2-preboot-init', 'scripts/pack-sq2-lte-fs12.py', 'scripts/pack-sq2-lte-fsg.py', 'scripts/prepare-sq2-el2-preboot-ram.py', 'tests/test_sq2_lte_fs12_seed.py', 'tests/test_sq2_lte_fsg_seed.py']
class RetiredDiagnostics(unittest.TestCase):
 def test_files_removed(self):
  for name in RETIRED:self.assertFalse((ROOT/name).exists(),name)
 def test_no_active_references(self):
  names=[Path(n).name for n in RETIRED[:6]]
  for folder in ('scripts','probe'):
   for p in (ROOT/folder).rglob('*'):
    if not p.is_file() or '__pycache__' in p.parts:continue
    try:text=p.read_text()
    except UnicodeDecodeError:continue
    # A removal-only manifest is not an executable invocation of retired tools.
    # Keep scanning every other line, including callers that might reintroduce them.
    if p.name == 'surface-lte-payload.py':
     text='\n'.join(line for line in text.splitlines() if not line.startswith('RETIRED_RUNTIME = '))
    for name in names:self.assertNotIn(name,text,str(p))
 def test_shared_paths_retained(self):
  for n in ('scripts/prepare-sq2-el2-preboot.py','scripts/build.py','scripts/prepare-sq2-venus-system-pm-boot.py','probe/lte/lte-unit-seed','probe/lte/lte-unit-guard','probe/lte/lte-framework-boot'):
   self.assertTrue((ROOT/n).is_file(),n)
if __name__=='__main__':unittest.main()
