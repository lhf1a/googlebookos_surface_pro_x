import pathlib,subprocess,tempfile,unittest,shutil
ROOT=pathlib.Path(__file__).resolve().parents[1]
class TestMfsReader(unittest.TestCase):
    def test_fixed_read_frame(self):
        if not shutil.which('gcc'):self.skipTest('native gcc required')
        with tempfile.TemporaryDirectory() as d:
            exe=pathlib.Path(d)/'mfs'
            subprocess.run(['gcc','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',str(ROOT/'probe/test2/sq2-mfs-readonly-lab.c'),'-o',str(exe)],check=True,capture_output=True)
            r=subprocess.run([str(exe),'--self-test'],check=True,capture_output=True,text=True)
            self.assertIn('write opcode refused',r.stdout)
            r=subprocess.run([str(exe),'--frame-fixture'],check=True,capture_output=True,text=True)
            self.assertEqual(r.stdout.strip(),'01080006002f746573740010040000800000')
    def test_build_does_not_deploy(self):
        s=(ROOT/'scripts/build-sq2-mfs-readonly-lab.sh').read_text()
        self.assertNotIn('ssh ',s);self.assertNotIn('scp ',s)
if __name__=='__main__':unittest.main()
