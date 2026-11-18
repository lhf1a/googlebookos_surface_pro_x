"""Execute the actual discovery helper without opening any hardware device."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

R = Path(__file__).resolve().parents[1]


class DeviceIds(unittest.TestCase):
    def test_protocol_gate_and_actual_transport(self):
        for devices, expected in [
            ([('045E', '0C21', True)], '045E:0C21'),
            ([('045E', '0996', True)], '045E:0996'),
            ([('1234', 'ABCD', False), ('1234', '1234', True)], '1234:1234'),
            ([('045E', '0996', False)], None),
            ([('045E', '0996', True), ('1234', '1234', True)], None),
        ]:
            with self.subTest(devices=devices), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                for path in ('run', 'logs', 'dev', 'sys/class/hidraw'):
                    (root/path).mkdir(parents=True, exist_ok=True)
                for i, (vendor, product, supports_ipts) in enumerate(devices, 19):
                    node = 'hidraw'+str(i)
                    dev = root/f'sys/devices/spi7.{i}/hid'
                    dev.mkdir(parents=True)
                    (dev.parent/'ready').write_text('ready\n')
                    (dev/'uevent').write_text(f'HID_ID=001C:0000{vendor}:0000{product}\n')
                    (dev/'report_descriptor').write_bytes(b'fixture')
                    (root/'sys/class/hidraw'/node).mkdir()
                    (root/'sys/class/hidraw'/node/'device').symlink_to(dev)
                    (root/'dev'/node).write_text('IPTS' if supports_ipts else 'other')
                script = (R/'probe/test2/touch-services').read_text()
                for old, new in [('/dev/uinput','/dev/null'), ('/sys/', str(root/'sys')+'/'),
                                 ('/run/', str(root/'run')+'/'), ('/logs/', str(root/'logs')+'/'),
                                 ('node=/dev/', 'node='+str(root/'dev')+'/'),
                                 ('iptsd-check-device','probe_device')]:
                    script = script.replace(old, new)
                # Stop before starting the worker; test the entire selection.
                script = script[:script.index('iptsd "$selected"')]+'exit 0\n'
                stubs = '''sleep() { :; }
timeout() { shift; "$@"; }
probe_device() { [ "$1 $2" = "-t touchscreen" ] && [ "$(cat "$3")" = IPTS ]; }
'''
                p = subprocess.run(['sh'], input=stubs+script, text=True, capture_output=True)
                self.assertEqual(p.returncode, 0 if expected else 2, p.stderr)
                if expected:
                    self.assertEqual((root/'run/iptsd-hid-id').read_text().strip(), expected)
                    ready = Path((root/'run/iptsd-ready').read_text().strip())
                    self.assertTrue(ready.is_file())
                    self.assertRegex(str(ready), r'/spi7\.(19|20)/ready$')
                else:
                    self.assertFalse((root/'run/iptsd-hid-id').exists())

    def test_refreshes_only_selected_virtual_nodes(self):
        source = (R/'probe/test2/refresh-iptsd-input-nodes').read_text()
        choices = re.search(r'case "\$name" in (.*?)\) ;;', source)[1]
        shell = 'hid_id="$1"; name="$2"; case "$name" in '+choices+') exit 0;; *) exit 1;; esac'
        for name, code in [('IPTSD Virtual Touchscreen 1234:ABCD', 0),
                           ('IPTSD Virtual Stylus 1234:ABCD', 0),
                           ('IPTSD Virtual Touchscreen 045E:0996', 1), ('spi Stylus', 1)]:
            self.assertEqual(subprocess.run(['sh','-c',shell,'test','1234:ABCD',name]).returncode, code)
