import importlib.util
from pathlib import Path
import unittest
import tempfile
import subprocess

ROOT=Path(__file__).resolve().parents[1]
def load(name):
    s=importlib.util.spec_from_file_location(name,ROOT/'scripts'/name)
    m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
k=load('sq2-ksu-boot-payload.py')
d=load('sq2-display-boot-payload.py')
v=load('sq2-video-boot-payload.py')
u=load('sq2-usb-boot-payload.py')

class KsuMenu(unittest.TestCase):
    def test_real_combined_menu_and_rescue(self):
        base=(ROOT/'configs/grub-source.cfg').read_text()
        for profile in d.PROFILES:
            menu=d.default_menu(u.default_menu(v.default_menu(base)),profile)
            result=k.menu(menu,True)
            d.validate_menu(result.encode(),profile)
            for block in d.blocks(result):
                if 'sq2.rescue=1' in block[0]:
                    self.assertIn('/Image-rescue ',block[0])
                elif 'linux ' in block[0]:
                    self.assertIn('/Image ',block[0])
                    self.assertNotIn('/Image-rescue ',block[0])
            self.assertEqual(k.menu(menu,False),menu)

    def test_refuse_missing_selinux_or_rescue(self):
        menu=(ROOT/'configs/grub-source.cfg').read_text()
        for bad in (menu.replace('lsm=selinux',''),menu.replace('sq2.rescue=1','')):
            with self.assertRaises(ValueError): k.menu(bad,True)

class Bootstrap(unittest.TestCase):
    def test_first_boot_then_preserve_existing(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td); (p/'run').mkdir(); (p/'payload').mkdir(); (p/'bin').mkdir()
            (p/'data/local/tmp').mkdir(parents=True)
            (p/'cmdline').write_text('suspend.screen_only=1 nohibernate')
            (p/'guard').write_text('Y\n')
            source=(ROOT/'probe/test2/ksu-framework-boot').read_text()
            for name in k.FILES:
                (p/'payload'/name).write_text(name)
            source=source.replace('/proc/cmdline',str(p/'cmdline')).replace('/run/ksu-bootstrap-lock',str(p/'run/lock'))
            source=source.replace('/sys/module/suspend/parameters/screen_only',str(p/'guard'))
            source=source.replace('/etc/sq2-ksu',str(p/'payload')).replace('/proc/1/root/data',str(p/'data'))
            source=source.replace('export PATH=/bin:/sbin','export PATH='+str(p/'bin')+':/usr/bin:/bin')
            fake=p/'bin/nsenter'
            fake.write_text('''#!/bin/sh
while [ "$1" != -- ]; do shift; done
shift
case "$*" in
 */getprop*) echo 1;;
 */cmd*) echo 10;;
 *'pm list packages -u'*) [ ! -f "ROOT/manager" ] || echo package:com.rifsxd.ksunext;;
 *'pm list users'*) printf 'UserInfo{0:Owner:13}\nUserInfo{10:User:10}\n';;
 *'pm list packages --user 0'*) [ ! -f "ROOT/user0" ] || echo package:com.rifsxd.ksunext;;
 *'pm list packages --user 10'*) [ ! -f "ROOT/manager" ] || echo package:com.rifsxd.ksunext;;
 *'pm install-existing --user 0'*) touch ROOT/user0; echo enable0 >> ROOT/actions;;
 *'pm install --user 10'*) touch ROOT/manager; echo manager >> ROOT/actions;;
 *'debug version'*) echo 'Kernel Version: 33129';;
 *' install') mkdir -p ROOT/data/adb; echo installed > ROOT/data/adb/ksud; echo daemon >> ROOT/actions;;
 *) exit 7;;
esac
exit 0
'''.replace('ROOT',str(p)))
            fake.chmod(0o755)
            for _ in range(2):
                subprocess.run(['sh','-c',source],check=True,capture_output=True)
            self.assertEqual((p/'actions').read_text(),'daemon\nmanager\nenable0\n')
            self.assertEqual(list((p/'data/local/tmp').iterdir()),[])
            (p/'cmdline').write_text('sq2.ksu=0')
            subprocess.run(['sh','-c',source],check=True,capture_output=True)
            self.assertEqual((p/'actions').read_text(),'daemon\nmanager\nenable0\n')

if __name__=='__main__': unittest.main()
