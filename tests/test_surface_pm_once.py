import importlib.util
from pathlib import Path
import unittest
R=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('surface_once',R/'scripts/prepare-surface-pm-once.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

def menu(name,uuid,path):
    return (f"set default={name}\nset timeout=10\n"
            f"menuentry 'normal' --id {name} {{\n search --fs-uuid --set=root {uuid}\n chainloader {path}\n}}\n"
            "menuentry 'fallback' --id retained-fallback {\n chainloader /EFI/retained/fallback.efi\n}\n").encode()

class SurfaceOnce(unittest.TestCase):
    def test_arbitrary_existing_identity_is_preserved_not_selected(self):
        for name,uuid,path in [('any-sq1','1111-2222','/EFI/alpha/normal.efi'),
                               ('any-sq2','3333-4444','/EFI/beta/normal.efi'),
                               ('unlabelled','5555-6666','/EFI/unrelated/normal.efi')]:
            original=menu(name,uuid,path)
            result,env=m.prepare(original)
            self.assertIn(('set default='+name).encode(),result)
            self.assertIn(uuid.encode(),result)
            self.assertIn(('else\n  chainloader '+path).encode(),result)
            self.assertIn(b' fi\n}\n',result)
            self.assertNotIn(b'fi}',result)
            self.assertEqual(result.split(b"menuentry 'fallback'")[1],original.split(b"menuentry 'fallback'")[1])
            self.assertEqual(len(env),1024)
            self.assertIn(b'surface_pm_once=1\n',env)

    def test_fail_closed_consume_and_readback_before_chain(self):
        result,_=m.prepare(menu('default','A1B2-C3D4','/EFI/old.efi'))
        self.assertLess(result.index(b'save_env'),result.index(b'set surface_pm_once=unknown'))
        self.assertLess(result.index(b'set surface_pm_once=unknown'),result.index(b'chainloader /EFI/SurfacePmAuto'))
        with self.assertRaises(ValueError):m.prepare(result)
        with self.assertRaises(ValueError):m.prepare(menu('d','A-B','/EFI/old.efi'),launcher='/EFI/../bad.efi')
