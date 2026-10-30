import pathlib
import subprocess
import sys
import tempfile
import unittest

SCRIPT=pathlib.Path(__file__).resolve().parents[1]/'scripts/build-sq2-edp-edid65.py'

def fixture():
    b=bytearray(128)
    b[:8]=bytes.fromhex('00ffffffffffff00')
    b[8:12]=bytes.fromhex('30e45505')
    b[54:56]=(35550).to_bytes(2,'little')
    b[56:72]=bytes.fromhex('4078b08037703020550412b710000019')
    b[72:77]=bytes.fromhex('000000fd00')
    b[78]=60;b[81]=36;b[113:126]=b'LP129WT112684'
    b[127]=(-sum(b[:127]))&255
    return bytes(b)

class Edid65Test(unittest.TestCase):
    def run_builder(self,source,output,*args):
        return subprocess.run([sys.executable,str(SCRIPT),str(source),str(output),*args],capture_output=True,text=True)

    def test_70_exact_clock_and_reject_unrequested_rates(self):
        with tempfile.TemporaryDirectory() as d:
            source=pathlib.Path(d)/'original.bin';out=pathlib.Path(d)/'70.bin'
            old=fixture();source.write_bytes(old)
            result=self.run_builder(source,out,'--hz','70')
            self.assertEqual(result.returncode,0,result.stderr)
            new=out.read_bytes()
            self.assertEqual(int.from_bytes(new[54:56],'little')*10,414750)
            self.assertEqual((new[78],new[81]),(70,42))
            self.assertEqual(sum(new)%256,0)
            self.assertEqual(source.read_bytes(),old)
            for hz in ('60','75','90','120'):
                target=pathlib.Path(d)/(hz+'.bin')
                self.assertNotEqual(self.run_builder(source,target,'--hz',hz).returncode,0)
                self.assertFalse(target.exists())

    def test_scoped_delta_and_original_preserved(self):
        with tempfile.TemporaryDirectory() as d:
            source=pathlib.Path(d)/'original.bin';out=pathlib.Path(d)/'65.bin'
            old=fixture();source.write_bytes(old)
            r=self.run_builder(source,out)
            self.assertEqual(r.returncode,0,r.stderr)
            new=out.read_bytes()
            self.assertEqual(source.read_bytes(),old)
            self.assertEqual(sum(new)%256,0)
            self.assertEqual([i for i,(a,b) in enumerate(zip(old,new)) if a!=b],[54,55,78,81,127])
            self.assertEqual(int.from_bytes(new[54:56],'little')*10,385120)
            self.assertEqual((new[78],new[81]),(65,39))
            self.assertNotEqual(self.run_builder(source,out).returncode,0)
            self.assertEqual(out.read_bytes(),new)

    def test_wrong_input_rejected_without_output(self):
        for offset in (8,54,56,78,81,113,127):
            with self.subTest(offset=offset), tempfile.TemporaryDirectory() as d:
                source=pathlib.Path(d)/'original.bin';out=pathlib.Path(d)/'65.bin'
                bad=bytearray(fixture());bad[offset]^=1
                if offset!=127:bad[127]=(-sum(bad[:127]))&255
                source.write_bytes(bad)
                self.assertNotEqual(self.run_builder(source,out).returncode,0)
                self.assertFalse(out.exists())
                self.assertEqual(source.read_bytes(),bad)

if __name__=='__main__':unittest.main()
