"""Host-side tests for the first-boot userdata growth helper.

The real helper shell is executed; only the block/GPT tools are replaced by a
fixture. The fake sgdisk intentionally accepts long options only (this is how
the Android popt-based gptfdisk build behaves); any short-option call would
fail the same way it failed on 192.168.5.123. Nothing here writes a real disk.
"""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'probe/test2/grow-image-userdata'
DISK_SECTORS = 500118192
LAST_USABLE = DISK_SECTORS - 34
PART_START = 33247232
PART_SIZE = 8695768
GUID = '15781A51-F0BB-4146-BEA9-D08602D7CBF3'
TYPE = '0FC63DAF-8483-4772-8E79-3D69D8477DE4'

FAKE_SGDISK = r'''import json,os,sys
from pathlib import Path
root=Path(os.environ['FIXTURE']); state_path=root/'gpt.json'; calls=root/'calls'
args=sys.argv[1:]
if os.environ.get('FAKE_FAIL')=='resize2fs' and args[0]=='resize2fs':
    sys.exit(9)
with calls.open('a') as f:f.write(json.dumps(args)+'\n')
if args[0]!='sgdisk':
    sys.exit(0)
state=json.loads(state_path.read_text())
parts=state['partitions']; pretend='--pretend' in args
i=1
while i<len(args):
    a=args[i]
    if a=='--pretend': i+=1; continue
    if a=='--move-second-header':
        state['last_usable']=state['disk_sectors']-34; i+=1; continue
    if a=='--print':
        print('Disk /dev/fixture: %d sectors, 238.5 GiB'%state['disk_sectors'])
        print('First usable sector is 34, last usable sector is %d'%state['last_usable'])
        i+=1; continue
    if a=='--info':
        n=args[i+1]
        if os.environ.get('FAKE_FAIL')=='info':
            sys.stderr.write('simulated info failure\n'); sys.exit(3)
        if n not in parts:
            sys.stderr.write('No such partition\n'); sys.exit(4)
        p=parts[n]
        print('Partition GUID code: %s (Linux filesystem)'%p['type'])
        print('Partition unique GUID: %s'%p['guid'])
        print('First sector: %d (at 15.9 GiB)'%p['start'])
        print('Last sector: %d (at 20.0 GiB)'%(p['start']+p['size']-1))
        print('Partition size: %d sectors (4.1 GiB)'%p['size'])
        print('Attribute flags: %s'%p['attrs'])
        print("Partition name: '%s'"%p['name'])
        i+=2; continue
    if a=='--delete':
        parts.pop(args[i+1],None); i+=2; continue
    if a=='--new':
        n,s,e=args[i+1].split(':'); parts[n]={'start':int(s),'size':int(e)-int(s)+1,
            'guid':'','type':'','attrs':'0000000000000000','name':''}
        i+=2; continue
    if a=='--partition-guid':
        n,g=args[i+1].split(':',1); parts[n]['guid']=g; i+=2; continue
    if a=='--typecode':
        n,t=args[i+1].split(':',1); parts[n]['type']=t; i+=2; continue
    if a=='--change-name':
        n,v=args[i+1].split(':',1); parts[n]['name']=v; i+=2; continue
    if a=='--attributes':
        n,rest=args[i+1].split(':',1); val=rest.split(':',1)[1]
        parts[n]['attrs']=val.replace('0x','').zfill(16); i+=2; continue
    if not a.startswith('-'):
        i+=1; continue
    sys.stderr.write('unexpected fake sgdisk argument: '+a+'\n'); sys.exit(5)
if not pretend:
    state_path.write_text(json.dumps(state))
sys.exit(0)
'''

BLOCKDEV = '#!/bin/sh\necho 512\n'
RESIZE_HELPER = r'''#!/bin/sh
echo "$@" >> "$FIXTURE/resize-calls"
printf '%s\n' "$(( $4 / 512 ))" > "$FIXTURE/sys/class/block/nvme0n1p1/size"
exit 0
'''


class GrowFixture(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        p = self.p = Path(self.temp.name)
        dev, sysroot, esp, run = p/'dev', p/'sys', p/'esp', p/'run'
        for d in (dev/'block/by-name', sysroot/'devices/platform/nvme0n1/nvme0n1p1',
                  sysroot/'class/block/nvme0n1', esp, run, p/'bin'):
            d.mkdir(parents=True, exist_ok=True)
        (dev/'nvme0n1').write_bytes(b'')
        (dev/'nvme0n1p1').write_bytes(b'')
        (dev/'block/by-name/userdata').symlink_to('../../nvme0n1p1')
        p1 = sysroot/'devices/platform/nvme0n1/nvme0n1p1'
        for name, value in (('partition', '1'), ('start', str(PART_START)),
                            ('size', str(PART_SIZE)), ('dev', '259:1')):
            (p1/name).write_text(value + '\n')
        (sysroot/'devices/platform/nvme0n1/size').write_text(str(DISK_SECTORS) + '\n')
        (sysroot/'class/block/nvme0n1/size').write_text(str(DISK_SECTORS) + '\n')
        (sysroot/'class/block/nvme0n1p1').symlink_to('../../devices/platform/nvme0n1/nvme0n1p1')
        (sysroot/'class/block/nvme0n1/nvme0n1p1').symlink_to('../../../devices/platform/nvme0n1/nvme0n1p1')
        (esp/'SPX-IMAGE.txt').write_text('SPX_IMAGE_FORMAT=1\n')
        (run/'sq2-ssd-device').write_text(str(dev/'nvme0n1'))
        (p/'cmdline').write_text('sq2.native=ssd sq2.el2=1\n')
        (p/'mountinfo').write_text('1 0 259:1 / /data rw - ext4 /dev/x rw\n')
        (p/'gpt.json').write_text(json.dumps({
            'disk_sectors': DISK_SECTORS, 'last_usable': 41943006,
            'partitions': {'1': {'start': PART_START, 'size': PART_SIZE,
                                 'guid': GUID, 'type': TYPE,
                                 'attrs': '0000000000000000', 'name': 'userdata'}}}))
        (p/'fake_sgdisk.py').write_text(FAKE_SGDISK)
        (p/'bin/blockdev').write_text(BLOCKDEV)
        (p/'bin/blockdev').chmod(0o755)
        (p/'bin/resize-userdata-partition').write_text(RESIZE_HELPER)
        (p/'bin/resize-userdata-partition').chmod(0o755)
        text = SOURCE.read_text()
        text = text.replace('a() { nsenter -t 1 -m -r -- /system/bin/"$@"; }',
                            'a() { "$PYTHON" "$FAKE_BIN" "$@"; }')
        text = text.replace('a cat /proc/self/mountinfo', 'cat "$FIXTURE/mountinfo"')
        text = text.replace('/proc/cmdline', str(p/'cmdline'))
        text = text.replace('/run/sq2-ssd-device', str(run/'sq2-ssd-device'))
        text = text.replace('/dev/block/by-name/userdata', str(dev/'block/by-name/userdata'))
        text = text.replace('/sys/class/block', str(sysroot/'class/block'))
        text = text.replace('[ "$disk" = "/dev/$parent" ]', '[ "$disk" = "$DEVROOT/$parent" ]')
        text = text.replace('/media/esp', str(esp))
        text = text.replace('/run/spx-userdata-ready', str(run/'spx-userdata-ready'))
        self.script = p/'helper'
        self.script.write_text(text)
        self.env = dict(os.environ, FIXTURE=str(p), FAKE_BIN=str(p/'fake_sgdisk.py'),
                        PYTHON=sys.executable, DEVROOT=str(dev),
                        PATH=str(p/'bin') + ':' + os.environ.get('PATH', ''))

    def run_helper(self, **extra):
        return subprocess.run(['sh', str(self.script)], env=dict(self.env, **extra),
                              capture_output=True, text=True)

    def state(self):
        return json.loads((self.p/'gpt.json').read_text())

    def calls(self):
        path = self.p/'calls'
        return [json.loads(x) for x in path.read_text().splitlines()] if path.exists() else []

    def sgdisk_calls(self):
        return [c for c in self.calls() if c and c[0] == 'sgdisk']

    def test_grows_entry_and_preserves_identity(self):
        r = self.run_helper()
        self.assertEqual(r.returncode, 0, r.stderr)
        new_size = LAST_USABLE - PART_START + 1
        self.assertIn('USERDATA_GROWN', r.stdout)
        self.assertIn('sectors=%d' % new_size, r.stdout)
        part = self.state()['partitions']['1']
        self.assertEqual(part['start'], PART_START)
        self.assertEqual(part['size'], new_size)
        self.assertEqual(part['guid'], GUID)
        self.assertEqual(part['type'], TYPE)
        self.assertEqual(part['attrs'], '0000000000000000')
        self.assertEqual(part['name'], 'userdata')
        self.assertEqual(self.state()['last_usable'], LAST_USABLE)
        flags = [arg for c in self.sgdisk_calls() for arg in c[1:]]
        for wanted in ('--move-second-header', '--delete', '--new', '--partition-guid',
                       '--typecode', '--change-name', '--attributes'):
            self.assertIn(wanted, flags)
        self.assertEqual((self.p/'esp/SPX-USERDATA-GROWN.txt').read_text().strip(),
                         '%d:1:%d:%d' % (DISK_SECTORS, PART_START, new_size))
        self.assertTrue((self.p/'run/spx-userdata-ready').exists())
        resize = (self.p/'resize-calls').read_text().split()
        self.assertEqual(resize[-2:], [str(new_size*512), str(PART_START*512)][::-1])
        self.assertTrue(any(c[0] == 'resize2fs' for c in self.calls()))

    def test_marker_shortcut_skips_all_gpt_work(self):
        (self.p/'esp/SPX-USERDATA-GROWN.txt').write_text(
            '%d:1:%d:%d\n' % (DISK_SECTORS, PART_START, PART_SIZE))
        r = self.run_helper()
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertTrue((self.p/'run/spx-userdata-ready').exists())
        self.assertEqual(self.calls(), [])
        self.assertEqual(self.state()['partitions']['1']['size'], PART_SIZE)

    def test_short_option_regression_guard(self):
        text = SOURCE.read_text()
        self.assertIsNone(re.search(r'sgdisk\s+-[A-Za-z]', text),
                          'sgdisk short options are not supported by the Android gptfdisk build')
        for wanted in ('sgdisk --info', 'sgdisk --print', 'sgdisk --move-second-header',
                       'sgdisk --delete', '--new ', '--partition-guid', '--typecode',
                       '--change-name', '--attributes'):
            self.assertIn(wanted, text)

    def test_overlap_after_userdata_is_rejected(self):
        other = self.p/'sys/devices/platform/nvme0n1/nvme0n1p2'
        other.mkdir()
        (other/'partition').write_text('2\n')
        (other/'start').write_text(str(PART_START + 1000) + '\n')
        (other/'size').write_text('2000\n')
        (self.p/'sys/class/block/nvme0n1/nvme0n1p2').symlink_to(
            '../../../devices/platform/nvme0n1/nvme0n1p2')
        r = self.run_helper()
        self.assertNotEqual(r.returncode, 0)
        self.assertFalse((self.p/'esp/SPX-USERDATA-GROWN.txt').exists())
        self.assertEqual(self.state()['partitions']['1']['size'], PART_SIZE)

    def test_info_failure_aborts_before_any_write(self):
        r = self.run_helper(FAKE_FAIL='info')
        self.assertNotEqual(r.returncode, 0)
        self.assertFalse((self.p/'esp/SPX-USERDATA-GROWN.txt').exists())
        self.assertEqual(self.state()['partitions']['1']['size'], PART_SIZE)
        self.assertTrue(any(c[0] == 'sgdisk' for c in self.calls()))
        self.assertFalse(any(c[0] == 'resize2fs' for c in self.calls()))

    def test_resize2fs_failure_keeps_marker_unwritten(self):
        r = self.run_helper(FAKE_FAIL='resize2fs')
        self.assertNotEqual(r.returncode, 0)
        self.assertFalse((self.p/'esp/SPX-USERDATA-GROWN.txt').exists())
        self.assertFalse((self.p/'run/spx-userdata-ready').exists())


if __name__ == '__main__':
    unittest.main()
