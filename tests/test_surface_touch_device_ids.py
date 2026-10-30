"""Test the actual helper match expressions without accessing hardware."""
from pathlib import Path
import re
import subprocess
import unittest

R = Path(__file__).resolve().parents[1]

class DeviceIds(unittest.TestCase):
    def test_hid_gate(self):
        source = (R/'probe/test2/touch-services').read_text()
        pattern = re.search(r"grep -qEi '([^']+)'", source)[1]
        for product, bus, expected in [('0C21','001C',0), ('0996','001C',0),
                                       ('0c21','001c',0), ('0997','001C',1),
                                       ('0996','0003',1)]:
            rc = subprocess.run(['grep','-qEi',pattern], input=f'HID_ID={bus}:0000045E:0000{product}\n',text=True).returncode
            self.assertEqual(rc, expected)

    def test_node_gate(self):
        source=(R/'probe/test2/refresh-iptsd-input-nodes').read_text()
        choices=re.search(r'case "\$name" in (.*?)\) ;;',source)[1]
        shell='name="$1"; case "$name" in '+choices+') exit 0;; *) exit 1;; esac'
        for product in ('0C21','0996','0997'):
            for kind in ('Touchscreen','Stylus'):
                name=f'IPTSD Virtual {kind} 045E:{product}'
                self.assertEqual(subprocess.run(['sh','-c',shell,'test',name]).returncode,0 if product!='0997' else 1)
        self.assertEqual(subprocess.run(['sh','-c',shell,'test','spi 045E:0996 Stylus']).returncode,1)

if __name__=='__main__': unittest.main()
