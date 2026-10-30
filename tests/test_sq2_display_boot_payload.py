from pathlib import Path
import gzip
import importlib.util
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
def load(name, file):
    s = importlib.util.spec_from_file_location(name, ROOT/'scripts'/file)
    m = importlib.util.module_from_spec(s); s.loader.exec_module(m)
    return m
d = load('display', 'sq2-display-boot-payload.py')
c = load('cpio', 'pack-sq2-vulkan.py')
v = load('video', 'sq2-video-boot-payload.py')
deploy = load('deploy', 'deploy-experimental-usb.py')
MENU = (ROOT/'configs/grub-source.cfg').read_text()

def archive(files):
    raw = b''.join(c.entry(n,b,m) for n,(m,b) in files.items())+c.entry('TRAILER!!!',b'',0)
    return gzip.compress(raw,mtime=0)

class Profiles(unittest.TestCase):
    def test_both_defaults_and_video_rescue(self):
        for profile in d.PROFILES:
            menu = d.default_menu(v.default_menu(MENU), profile)
            d.validate_menu(menu.encode(), profile)
            self.assertEqual(menu.count(d.OVERRIDE),1)
            self.assertEqual(menu.count(d.ADDON),1)
            self.assertIn('sq2.cpufreq=0',menu)
            self.assertIn("menuentry 'Power off' { halt }",menu)
            policy={'schema':1,'enabled':True,'menu_sha256':d.sha(menu.encode())}
            deploy.validate_video_menu(menu.encode(), policy)

    def test_mutations_rejected(self):
        good=d.default_menu(MENU)
        for bad in (good.replace('nohibernate',''),good.replace('suspend.screen_only=1',''),
                    good.replace(d.OVERRIDE,''),good.replace(' '+d.BOOT+d.ADDON,''),
                    good.replace('set default=sq2-display-65','set default=sq2-qcom'),
                    good.replace('sq2.rescue=1','sq2.rescue=1 '+d.OVERRIDE),
                    good.replace('sq2.display=2.7-60','sq2.display=2.7-60 sq2.display=5.4-65'),
                    good+'linux /Image\n'):
            with self.assertRaises(ValueError):d.validate_menu(bad.encode(),d.DEFAULT)

    def test_reject_ambiguous_input(self):
        for bad in (MENU.replace('set default=sq2-qcom',''),MENU+d.OVERRIDE,
                    MENU.replace('suspend.screen_only=1','')):
            with self.assertRaises(ValueError):d.default_menu(bad)
        with self.assertRaises(ValueError):d.default_menu(MENU,'70')
        stock=d.default_menu(MENU,'2.7-60').replace('set default=sq2-qcom','set default=sq2-rescue')
        with self.assertRaises(ValueError):d.validate_menu(stock.encode(),'2.7-60')

    def test_addon_exact_delta_credentials_last(self):
        original={d.MODULE:(0o100644,b'stock'), 'init':(0o100755,b'keep-init'),
                  'etc/shadow':(0o100600,b'fixture-only'), 'bin/dropbear':(0o100755,b'fixture-ssh')}
        cred=archive({k:original[k] for k in ('etc/shadow','bin/dropbear')})
        base=archive(original)+cred
        with patch.object(d,'STOCK_SHA',d.sha(b'stock')),patch.object(d,'MODULE_SHA',d.sha(b'v6')),patch.object(d,'EDID_SHA',d.sha(b'edid')):
            addon=d.build_addon(base,c,b'v6',b'edid')
            after=c.entries(gzip.decompress(base+addon))
            self.assertEqual(after[d.MODULE][1],b'v6')
            self.assertEqual(after[d.EDID][1],b'edid')
            self.assertEqual(after['init'],original['init'])
            self.assertTrue(addon.endswith(cred))
            self.assertEqual(c.entries(gzip.decompress(base)),original)
            for mod,edid in ((b'tampered',b'edid'),(b'v6',b'tampered')):
                with self.assertRaises(ValueError):d.build_addon(base,c,mod,edid)
            with self.assertRaises(ValueError):d.build_addon(archive({d.MODULE:original[d.MODULE]}),c,b'v6',b'edid')

    def test_deploy_binds_packed_menu_and_payloads(self):
        menu=d.default_menu(MENU).encode();base=b'base';addon=b'addon'
        policy=d.policy(menu,base,addon,d.DEFAULT)
        manifest={'display_profiles':policy,'boot_payload_sha256':policy['boot_payload_sha256']}
        layout={'partitions':[{'name':'SQ2PROBE','offset':4096}]}
        for values,fail in (([menu,base,addon],False),([menu,base,b'bad'],True),([menu+b'\n'],True)):
            with patch.object(deploy.subprocess,'check_output',side_effect=values):
                if fail:
                    with self.assertRaises(ValueError):deploy.verify_display_profiles(manifest,layout,Path('fixture.img'))
                else:deploy.verify_display_profiles(manifest,layout,Path('fixture.img'))

if __name__=='__main__':unittest.main()
