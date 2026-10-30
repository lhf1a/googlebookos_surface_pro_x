"""Pure preflight tests; never invokes a disk write."""
import importlib.util
from pathlib import Path
import unittest
import hashlib

spec = importlib.util.spec_from_file_location('deploy', Path(__file__).resolve().parents[1]/'scripts/deploy-experimental-usb.py')
d = importlib.util.module_from_spec(spec); spec.loader.exec_module(d)

class Preflight(unittest.TestCase):
    def test_video_policy(self):
        menu=b'linux /Image sq2.video.c2=experimental sq2.video.autostart=1\nlinux /Image sq2.rescue=1\n'
        policy=dict(schema=1,enabled=True,menu_sha256=hashlib.sha256(menu).hexdigest())
        d.validate_video_menu(menu,policy)
        with self.assertRaises(ValueError):d.validate_video_menu(menu+b'#change',policy)
        for bad in (menu.replace(b'sq2.video.autostart=1',b''),menu.replace(b'sq2.rescue=1',b'sq2.rescue=1 sq2.video.autostart=1')):
            with self.assertRaises(ValueError):
                d.validate_video_menu(bad,dict(policy,menu_sha256=hashlib.sha256(bad).hexdigest()))
        with self.assertRaises(ValueError):d.verify_hardware_video({}, {}, None, True)

    def test_confirmation(self):
        disk = dict(name='/dev/FIXTURE', serial='SERIAL', size=128, model='fixture')
        token = 'ERASE ["/dev/FIXTURE","SERIAL",128]'
        d.confirm_erase(disk, token)
        for wrong in ('yes', '', 'ERASE SERIAL', token.replace('128','256')):
            with self.assertRaises(ValueError): d.confirm_erase(disk, wrong)
        for change in ({'name':'/dev/OTHER'}, {'serial':'OTHER'}, {'size':256}):
            with self.assertRaises(ValueError): d.confirm_erase(dict(disk, **change), token)

    def test_usb_guard(self):
        disk = dict(type='disk', tran='usb', ro=False, serial='fixture', **{'log-sec':512})
        self.assertTrue(d.safe_disk(disk))
        for change in ({'tran':'nvme'}, {'ro':True}, {'serial':''}, {'mountpoints':['/']},
                       {'children':[{'mountpoints':['/boot']}]}):
            self.assertFalse(d.safe_disk(dict(disk, **change)))

    def test_expand(self):
        layout = dict(image_bytes=64*1024**3, partitions=[
            dict(name='super', index=3, offset=1024**2, bytes=1024**3),
            dict(name='userdata', index=1, offset=2*1024**3, bytes=61*1024**3)])
        data, end = d.growth(layout, 128*1024**3)
        self.assertEqual(data['index'],1)
        self.assertLess(end*512,128*1024**3)
        self.assertGreater((end+1)*512-data['offset'],data['bytes'])
        with self.assertRaises(ValueError): d.growth(layout,32*1024**3)
        layout['partitions'].append(dict(name='bad', index=7,offset=63*1024**3,bytes=4096))
        with self.assertRaises(ValueError): d.growth(layout,128*1024**3)

if __name__ == '__main__': unittest.main()
