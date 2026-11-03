import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT/'probe/test2/surface-swap-boot'
SIZE = 6442450944

# Only privileged/block-allocation operations are mocked. The real shell,
# permissions, exclusive creation, files, stat, awk and rename are exercised.
FAKE = r"""import os,sys,subprocess,json
from pathlib import Path
args=sys.argv[1:]; op=args[0]; root=Path(os.environ['FIXTURE'])
with (root/'calls').open('a') as f:f.write(json.dumps(args)+'\n')
if op=='stat' and args[-1]==str(root/'userdata'):
 print('103 1');sys.exit(0)
if op=='df':
 print('Filesystem 1024-blocks Used Available Capacity Mounted on')
 print('fixture 30000000 1 '+os.environ.get('FREE_KIB','20000000')+' 1% '+str(root/'data'));sys.exit(0)
if op=='dd':
 path=next(a[3:] for a in args if a.startswith('of='))
 with open(path,'r+b') as f:f.truncate(1024 if os.environ.get('FAIL')=='dd' else 6442450944)
 sys.exit(2 if os.environ.get('FAIL')=='dd' else 0)
if op=='mkswap':
 if os.environ.get('FAIL')=='mkswap':sys.exit(3)
 with open(args[-1],'r+b') as f:f.seek(4086);f.write(b'SWAPSPACE2')
 sys.exit(0)
if op=='swapon':
 if os.environ.get('FAIL')=='swapon':sys.exit(4)
 with open(args[-1],'rb') as f:f.seek(4086);valid=f.read(10)==b'SWAPSPACE2'
 if not valid:sys.exit(5)
 (root/'swaps').write_text('Filename Type Size Used Priority\n'+args[-1]+' file 6291452 0 -2\n');sys.exit(0)
p=subprocess.run(['busybox']+args,capture_output=True)
if op=='stat' and p.returncode==0 and '%u' in args[args.index('-c')+1]:
 fmt=args[args.index('-c')+1]
 # The unprivileged fixture owns these files; production requires real UID 0.
 out=p.stdout.decode();out=out.replace(str(os.getuid())+':','0:',1)
 sys.stdout.write(out)
else:sys.stdout.buffer.write(p.stdout)
sys.stderr.buffer.write(p.stderr);sys.exit(p.returncode)
"""


class SwapFixture(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.p=Path(self.temp.name);self.data=self.p/'data';self.data.mkdir()
        self.directory=self.data/'.surface-swap';self.file=self.directory/'swapfile'
        self.swaps=self.p/'swaps';self.swaps.write_text('Filename Type Size Used Priority\n')
        (self.p/'mountinfo').write_text('1 0 259:1 / '+str(self.data)+' rw - ext4 /dev/fixture rw\n')
        (self.p/'fake.py').write_text(FAKE)
        text=SOURCE.read_text().replace('bb() { /debug_ramdisk/busybox "$@"; }',
                                      'bb() { "$PYTHON" "$FAKE_BB" "$@"; }')
        text=text.replace('/dev/block/by-name/userdata',str(self.p/'userdata'))
        text=text.replace('/proc/self/mountinfo',str(self.p/'mountinfo'))
        text=text.replace('/proc/swaps',str(self.swaps)).replace('/data',str(self.data))
        self.script=self.p/'helper';self.script.write_text(text)
        self.env=dict(os.environ,FIXTURE=str(self.p),FAKE_BB=str(self.p/'fake.py'),PYTHON=sys.executable)

    def run_helper(self,**env):
        return subprocess.run(['sh',str(self.script),'--android-data'],env=dict(self.env,**env),capture_output=True,text=True)

    def calls(self,op):
        import json
        return [x for x in map(json.loads,(self.p/'calls').read_text().splitlines()) if x[0]==op]

    def test_create_repeat_and_reboot_reenable_without_reformat(self):
        r=self.run_helper();self.assertEqual(r.returncode,0,r.stderr)
        self.assertIn('SWAP_CREATED bytes='+str(SIZE),r.stdout);self.assertIn('SWAP_ENABLED',r.stdout)
        self.assertEqual(self.file.stat().st_size,SIZE);self.assertEqual(self.file.stat().st_mode&0o777,0o600)
        self.assertIn('count=6144',self.calls('dd')[0]);self.assertIn('conv=fsync',self.calls('dd')[0])
        r=self.run_helper();self.assertEqual(r.returncode,0,r.stderr);self.assertIn('SWAP_ALREADY_ACTIVE',r.stdout)
        self.swaps.write_text('Filename Type Size Used Priority\n')
        r=self.run_helper();self.assertEqual(r.returncode,0,r.stderr);self.assertIn('SWAP_ENABLED',r.stdout)
        self.assertEqual(len(self.calls('dd')),1);self.assertEqual(len(self.calls('mkswap')),1)
        self.assertEqual(len(self.calls('swapon')),2)

    def test_insufficient_space(self):
        r=self.run_helper(FREE_KIB='7340031');self.assertNotEqual(r.returncode,0)
        self.assertIn('insufficient-space',r.stderr);self.assertFalse(self.file.exists());self.assertFalse(self.calls('dd'))

    def test_wrong_device_and_readonly_data(self):
        for mount in ['1 0 259:2 / '+str(self.data)+' rw - ext4 /dev/other rw\n',
                      '1 0 259:1 / '+str(self.data)+' ro - ext4 /dev/fixture ro\n',
                      '1 0 259:1 / '+str(self.data)+' rw - tmpfs tmpfs rw\n']:
            (self.p/'mountinfo').write_text(mount)
            r=self.run_helper();self.assertNotEqual(r.returncode,0);self.assertIn('userdata-not-rw-ext4',r.stderr)
            self.assertFalse(self.directory.exists())

    def test_directory_symlink_preserves_victim(self):
        victim=self.p/'victim';victim.mkdir();(victim/'keep').write_text('pristine')
        self.directory.symlink_to(victim,target_is_directory=True)
        r=self.run_helper();self.assertNotEqual(r.returncode,0);self.assertIn('directory-symlink',r.stderr)
        self.assertEqual((victim/'keep').read_text(),'pristine')

    def test_existing_wrong_size_symlink_and_bad_header_never_reformatted(self):
        self.directory.mkdir(mode=0o700)
        self.file.write_text('preserve');self.file.chmod(0o600)
        r=self.run_helper();self.assertNotEqual(r.returncode,0);self.assertEqual(self.file.read_text(),'preserve')
        self.assertFalse(self.calls('mkswap'))
        self.file.unlink();self.file.symlink_to(self.p/'victim')
        r=self.run_helper();self.assertNotEqual(r.returncode,0);self.assertIn('existing-file-type',r.stderr)
        self.file.unlink()
        with self.file.open('wb') as f:f.truncate(SIZE)
        self.file.chmod(0o600)
        r=self.run_helper();self.assertNotEqual(r.returncode,0);self.assertIn('SWAP_ERROR: swapon',r.stderr)
        self.assertFalse(self.calls('mkswap'));self.assertFalse(self.calls('dd'))

    def test_failed_allocation_and_mkswap_cleanup_then_retry(self):
        for failure in ['dd','mkswap']:
            r=self.run_helper(FAIL=failure);self.assertNotEqual(r.returncode,0)
            self.assertFalse(self.file.exists());self.assertFalse((self.directory/'.swapfile.new').exists())
        self.assertEqual(self.run_helper().returncode,0)

    def test_swapon_failure_retains_valid_file_for_retry(self):
        r=self.run_helper(FAIL='swapon');self.assertNotEqual(r.returncode,0);self.assertTrue(self.file.exists())
        self.assertEqual(self.run_helper().returncode,0)
        self.assertEqual(len(self.calls('mkswap')),1)

    def test_private_interrupted_staging_recovered_but_symlink_refused(self):
        self.directory.mkdir(mode=0o700);tmp=self.directory/'.swapfile.new'
        victim=self.p/'victim';victim.write_text('preserve');tmp.symlink_to(victim)
        r=self.run_helper();self.assertNotEqual(r.returncode,0);self.assertEqual(victim.read_text(),'preserve')
        tmp.unlink();tmp.write_text('interrupted');tmp.chmod(0o600)
        self.assertEqual(self.run_helper().returncode,0);self.assertFalse(tmp.exists())


class BootGate(unittest.TestCase):
    def run_gate(self,mem=7592612,cmdline='sq2.native=ssd sq2.el2=1',guard='Y'):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'bin').mkdir();(p/'run').mkdir()
            for name,text in [('cmdline',cmdline),('meminfo',f'MemTotal: {mem} kB\n'),('guard',guard)]:
                (p/name).write_text(text)
            ns=p/'bin/nsenter';ns.write_text('#!/bin/sh\ncase "$*" in *getprop*) echo 1;; *) echo INNER_STAGE_REACHED;; esac\n');ns.chmod(0o755)
            text=SOURCE.read_text().replace('export PATH=/bin:/sbin',f'export PATH={p}/bin:/usr/bin:/bin')
            for old,new in [('/proc/cmdline',p/'cmdline'),('/proc/meminfo',p/'meminfo'),
                            ('/sys/module/suspend/parameters/screen_only',p/'guard'),('/run/surface-swap-lock',p/'run/lock')]:
                text=text.replace(old,str(new))
            script=p/'helper';script.write_text(text)
            return subprocess.run(['sh',str(script)],capture_output=True,text=True)

    def test_below_12g_gate(self):
        for mem in [1,4000000,7340031,7592612,8388608,12582911]:
            r=self.run_gate(mem=mem);self.assertEqual(r.returncode,0,r.stderr);self.assertIn('INNER_STAGE_REACHED',r.stdout)

    def test_12g_16g_skip_and_invalid_rejected(self):
        for mem in [12582912,12582913,15800000]:
            r=self.run_gate(mem=mem);self.assertEqual(r.returncode,0);self.assertIn('SWAP_SKIPPED_MEMORY',r.stdout)
        for mem in ['bad',0]:
            r=self.run_gate(mem=mem);self.assertNotEqual(r.returncode,0)

    def test_disabled_rescue_and_nonpersistent_skip(self):
        for cmd in ['sq2.native=ssd sq2.swap=0','sq2.native=ssd sq2.rescue=1','sq2.native=usb']:
            r=self.run_gate(cmdline=cmd);self.assertEqual(r.returncode,0,r.stderr);self.assertIn('SWAP_SKIPPED_',r.stdout)
            self.assertNotIn('INNER_STAGE_REACHED',r.stdout)

    def test_guard_not_disabled(self):
        r=self.run_gate(guard='N');self.assertNotEqual(r.returncode,0);self.assertNotIn('INNER_STAGE_REACHED',r.stdout)


if __name__=='__main__':unittest.main()
