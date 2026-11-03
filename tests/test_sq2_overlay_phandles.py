from pathlib import Path
import importlib.util
import os
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('phandles',ROOT/'scripts/sq2-overlay-phandles.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)


class Phandles(unittest.TestCase):
    def fixture(self,root):
        dts=root/'overlay.dts';dtbo=root/'overlay.dtbo'
        dts.write_text('''/dts-v1/; /plugin/;
/ { fragment@0 { target-path="/soc@0"; __overlay__ { venus-sq2@aa00000 {
 unrelated=<0xb5>; power-domains=<0xb5 0 0xb5 1 0xb5 2>;
 clocks=<1 227 1 225 1 226 0xb5 4 0xb5 2 0xb5 3 0xb5 0>;
 resets=<1 41 0xb5 0 1 42 1 43>;
}; }; }; };
''')
        subprocess.run(['dtc','-I','dts','-O','dtb','-o',dtbo,dts],check=True,capture_output=True)
        return dtbo.read_bytes()

    def test_exact_reference_offsets_preserve_unrelated_equal_numbers(self):
        with tempfile.TemporaryDirectory() as td:
            blob=self.fixture(Path(td));node='/fragment@0/__overlay__/venus-sq2@aa00000'
            offsets=m.videocc_offsets(blob,node,0xb5,1)
            self.assertEqual(len(offsets),8);self.assertEqual(len(set(offsets)),8)
            other=m.property_offsets(blob)[(node,'unrelated')][0]
            self.assertNotIn(other,offsets)
            candidate=bytearray(blob)
            for offset in offsets:struct.pack_into('>I',candidate,offset,0xb9)
            self.assertEqual(m.videocc_offsets(bytes(candidate),node,0xb9,1),offsets)
            self.assertEqual(candidate[other:other+4],struct.pack('>I',0xb5))
            # Identity/topology mismatch must fail, never blindly replace integers.
            for vid,gcc in [(0xb6,1),(0xb5,2)]:
                with self.assertRaises(ValueError):m.videocc_offsets(blob,node,vid,gcc)
            for bad in [b'',blob[:39],blob[:-4],b'BAD!'+blob[4:]]:
                with self.assertRaises(ValueError):m.property_offsets(bad)

    def test_actual_c_rebind_atomic_failure_and_bounds(self):
        with tempfile.TemporaryDirectory() as td:
            binary=Path(td)/'check'
            subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                            '-fno-pie','-no-pie','-I'+str(ROOT/'probe/video'),
                            ROOT/'tests/sq2_overlay_phandle_test.c','-o',binary],check=True,capture_output=True)
            subprocess.run([binary],check=True,capture_output=True)

    def test_startup_has_no_numeric_phandle_gate_and_keeps_no_retry(self):
        helper=(ROOT/'probe/test2/video-decoder-lab-start').read_text()
        stubs='''cat() { case "$1" in /proc/cmdline) echo "$CMDLINE";; */screen_only) echo Y;; *) echo boot;; esac; }
uname() { echo 6.3.8-sq2-android1; }; nsenter() { echo 1; }; cd() { :; }
sha256sum() { command cat >/dev/null; }
test() { case "$1" in -d|-e) return 1;; -L|-c) return 0;; *) command test "$@";; esac; }
mkdir() { :; }; od() { echo "$PHANDLE"; }; insmod() { echo "LOAD $*"; }
'''
        helper=helper.replace('> /tmp/sq2-native-pas-attempted','> /dev/null')
        for ph in ['000000b5','000000b9','00000000','ffffffff','bad','zzzzzzzz']:
            p=subprocess.run(['sh','-c',stubs+helper],text=True,capture_output=True,
                             env=dict(os.environ,CMDLINE='sq2.video.c2=experimental',PHANDLE=ph))
            self.assertEqual(p.returncode,0,p.stderr)
            self.assertEqual('LOAD sq2_hfi_overlay.ko' in p.stdout,p.returncode==0)
        self.assertNotIn('000000b5',helper)
        self.assertNotIn('od -An -tx1',helper)
        self.assertIn('RESIDENT_REFUSED',helper)
        self.assertIn('test ! -e /tmp/sq2-native-pas-attempted',helper)
        self.assertNotIn('rmmod',helper)


if __name__=='__main__':unittest.main()
