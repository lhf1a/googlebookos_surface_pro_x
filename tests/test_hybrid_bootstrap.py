import hashlib
import json
from pathlib import Path
import stat
import struct
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'scripts'))
import hybrid_bootstrap as b


class BootstrapTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def test_range_exact_and_source_unchanged(self):
        src = self.root/'src'; src.write_bytes(b'0123456789')
        b.copy_range(src,self.root/'slice',2,4)
        self.assertEqual((self.root/'slice').read_bytes(),b'2345')
        self.assertEqual(src.read_bytes(),b'0123456789')
        with self.assertRaises(ValueError): b.copy_range(src,self.root/'bad',9,2)
        with self.assertRaises(FileExistsError): b.copy_range(src,self.root/'slice',2,4)

    def test_sdk_zip_permissions_and_internal_link(self):
        archive = self.root/'sdk.zip'
        with zipfile.ZipFile(archive,'w') as z:
            f = zipfile.ZipInfo('bin/tool'); f.external_attr = (stat.S_IFREG|0o755)<<16
            z.writestr(f,b'example')
            link = zipfile.ZipInfo('bin/alias'); link.external_attr = (stat.S_IFLNK|0o777)<<16
            z.writestr(link,b'tool')
        b.unzip(archive,self.root/'sdk')
        self.assertEqual((self.root/'sdk/bin/tool').stat().st_mode & 0o777,0o755)
        self.assertEqual((self.root/'sdk/bin/alias').read_bytes(),b'example')

    def test_sdk_rejects_external_link(self):
        archive = self.root/'sdk.zip'
        with zipfile.ZipFile(archive,'w') as z:
            f = zipfile.ZipInfo('link'); f.external_attr = (stat.S_IFLNK|0o777)<<16
            z.writestr(f,b'../outside')
        with self.assertRaises(ValueError): b.unzip(archive,self.root/'sdk')

    def test_sdk_rejects_traversal(self):
        archive = self.root/'sdk.zip'
        with zipfile.ZipFile(archive,'w') as z: z.writestr('../outside',b'x')
        with self.assertRaises(ValueError): b.unzip(archive,self.root/'sdk')

    def sparse(self, bad=False):
        data = struct.pack('<I4H4I',0xed26ff3a,1,0,28,12,4096,3,3,0)
        data += struct.pack('<2H2I',0xcac1,0,1,4108)+b'A'*4096
        data += struct.pack('<2H2I',0xcac2,0,1,16)+b'ABCD'
        data += struct.pack('<2H2I',0xcac3,0,2 if bad else 1,12)
        src = self.root/'sparse';src.write_bytes(data);return src

    def test_sparse_raw_fill_zero(self):
        b.unsparse(self.sparse(),self.root/'raw')
        self.assertEqual((self.root/'raw').read_bytes(),b'A'*4096+b'ABCD'*1024+bytes(4096))

    def test_sparse_extent_rejected(self):
        with self.assertRaises(ValueError): b.unsparse(self.sparse(True),self.root/'raw')

    def lp(self, corrupt=False):
        # Real LP metadata format with one linear slot-A extent, not a mocked parser.
        data = bytearray(65536)
        geometry = bytearray(52)
        struct.pack_into('<II',geometry,0,0x616c4467,52)
        struct.pack_into('<III',geometry,40,4096,2,4096)
        geometry[8:40] = hashlib.sha256(geometry).digest()
        data[4096:4148] = geometry
        partition = bytearray(52);partition[:8] = b'system_a';struct.pack_into('<III',partition,40,0,1,0)
        extent = struct.pack('<QIQI',8,0,64,0)
        table = partition+extent
        header = bytearray(128);struct.pack_into('<IHHII',header,0,0x414c5030,10,0,128,0)
        struct.pack_into('<I',header,44,len(table));header[48:80] = hashlib.sha256(table).digest()
        struct.pack_into('<III',header,80,0,1,52);struct.pack_into('<III',header,92,52,1,24)
        header[12:44] = hashlib.sha256(header).digest()
        data[12288:12416] = header;data[12416:12416+len(table)] = table
        data[32768:36864] = b'R'*4096
        if corrupt:data[12420] ^= 1
        src=self.root/'super';src.write_bytes(data);return src

    def test_lp_hashes_and_extent_extraction(self):
        result=b.logical_partitions(self.lp(),self.root/'parts')
        self.assertEqual((self.root/'parts/system_a.img').read_bytes(),b'R'*4096)
        self.assertEqual(result['system_a']['bytes'],4096)

    def test_lp_corruption_rejected(self):
        with self.assertRaisesRegex(ValueError,'table hash'):b.logical_partitions(self.lp(True),self.root/'parts')

    def test_explicit_ndk_no_hidden_fallback(self):
        build=b.Bootstrap(None,self.root/'work')
        with self.assertRaisesRegex(ValueError,'Explicit staged NDK'):build.glue(self.root/'missing')
        self.assertFalse(build.report['image_built'])


if __name__ == '__main__':unittest.main()
