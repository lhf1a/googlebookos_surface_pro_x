"""The boot manifest may evolve without corrupting immutable runtime-v1."""
from pathlib import Path
import hashlib, importlib.util, tempfile, unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('pack',ROOT/'scripts/pack-sq2-c2-runtime-boot.py')
pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack)

class BootXmlTest(unittest.TestCase):
    def test_verified_device_candidate(self):
        self.assertEqual(hashlib.sha256(pack.boot_codec_xml()).hexdigest(),pack.CODEC_XML)
        self.assertIn(b'blocks-per-second" range="1-1036800',pack.boot_codec_xml())
    def test_reject_unknown_xml(self):
        original=pack.ROOT
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);(d/'configs').mkdir()
            (d/'configs/sq2-media-codecs-c2.xml').write_bytes(b'<MediaCodecs/>')
            try:
                pack.ROOT=d
                with self.assertRaisesRegex(ValueError,'unverified'):pack.boot_codec_xml()
            finally:pack.ROOT=original

if __name__=='__main__':unittest.main()
