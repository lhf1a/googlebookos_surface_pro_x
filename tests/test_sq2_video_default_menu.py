from pathlib import Path
import importlib.util,unittest
ROOT=Path(__file__).resolve().parents[1]
s=importlib.util.spec_from_file_location('video',ROOT/'scripts/sq2-video-boot-payload.py')
v=importlib.util.module_from_spec(s);s.loader.exec_module(v)
class DefaultMenu(unittest.TestCase):
    def test_enable_normal_preserve_rescue_and_optout(self):
        text='linux /Image nohibernate suspend.screen_only=1\nlinux /Image sq2.video=0\nlinux /Image sq2.rescue=1 sq2.cpufreq=0\n'
        out=v.default_menu(text)
        self.assertIn('sq2.video.c2=experimental sq2.video.autostart=1',out.splitlines()[0])
        self.assertEqual(out.splitlines()[1:],text.splitlines()[1:])
        self.assertEqual(v.default_menu(out),out)
    def test_require_rescue(self):
        with self.assertRaises(ValueError):v.default_menu('linux /Image\n')
    def test_reject_enabled_rescue(self):
        with self.assertRaises(ValueError):v.default_menu('linux /Image\nlinux /Image sq2.rescue=1 sq2.video.autostart=1\n')
if __name__=='__main__':unittest.main()
