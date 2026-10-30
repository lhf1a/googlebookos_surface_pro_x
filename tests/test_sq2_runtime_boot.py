"""Execute installer and startup opt-out guards in host fixtures, never hardware."""
from pathlib import Path
import hashlib, io, subprocess, tarfile, tempfile, unittest
ROOT=Path(__file__).resolve().parents[1]

class RuntimeBootTest(unittest.TestCase):
    def install(self, mode):
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp); src=d/'source';src.mkdir(); parent=d/'data'; target=parent/'v1'
            contents=b'tested-service'; sums=hashlib.sha256(contents).hexdigest()+'  service\n'
            with tarfile.open(src/'runtime.tar','w') as tar:
                for name,data in [('service',contents),('SHA256SUMS',sums.encode())]:
                    info=tarfile.TarInfo(name);info.size=len(data);tar.addfile(info,io.BytesIO(data))
            (src/'runtime.sha256').write_text(hashlib.sha256((src/'runtime.tar').read_bytes()).hexdigest()+'  runtime.tar\n')
            (src/'runtime-files.sha256').write_text(sums)
            (d/'guard').write_text('N' if mode=='guard' else 'Y')
            (d/'mounts').write_text('' if mode=='unmounted' else '/dev/fixture /data ext4 rw 0 0\n')
            if mode in ('existing','tampered'):
                target.mkdir(parents=True);(target/'service').write_bytes(contents if mode=='existing' else b'keep-bad')
                (target/'SHA256SUMS').write_text(sums)
            if mode=='symlink':
                other=d/'other';other.mkdir();parent.symlink_to(other,target_is_directory=True)
            if mode=='archive':(src/'runtime.tar').write_bytes(b'bad')
            code=(ROOT/'probe/video/sq2-c2-runtime-install.sh').read_text()
            code=code.replace('source=/vendor/etc/sq2-codec2','source='+str(src)).replace('parent=/data/vendor/sq2-codec2','parent='+str(parent))
            code=code.replace('/sys/module/suspend/parameters/screen_only',str(d/'guard')).replace('/proc/mounts',str(d/'mounts'))
            p=subprocess.run(['sh','-c',code],capture_output=True,text=True)
            if mode in ('fresh','existing'):
                self.assertEqual(p.returncode,0,p.stderr);self.assertEqual((target/'service').read_bytes(),contents)
            else:
                self.assertNotEqual(p.returncode,0)
                if mode=='tampered':self.assertEqual((target/'service').read_bytes(),b'keep-bad')
                else:self.assertFalse(target.exists())

    def test_install_and_reuse(self):
        for mode in ('fresh','existing'):self.install(mode)
    def test_rejections_preserve_state(self):
        for mode in ('guard','unmounted','archive','tampered','symlink'):self.install(mode)
    def test_framework_noop_guards(self):
        source=(ROOT/'probe/test2/video-runtime-framework-boot').read_text()
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp)
            for cmd in ('','sq2.video.c2=experimental','sq2.video.autostart=1',
                        'sq2.video.c2=experimental sq2.video.autostart=1 sq2.video=0',
                        'sq2.video.c2=experimental sq2.video.autostart=1 sq2.rescue=1'):
                (d/'cmdline').write_text(cmd)
                code=source.replace('/proc/cmdline',str(d/'cmdline')).replace('export PATH=/bin:/sbin','export PATH=/usr/bin:/bin')
                p=subprocess.run(['sh','-c',code],capture_output=True,text=True)
                self.assertEqual(p.returncode,0,p.stderr)
                self.assertEqual(p.stdout,'')

if __name__=='__main__':unittest.main()
