"""Verify module replacement preserves effective newc metadata, including credential replay."""
import gzip
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("pack_display", ROOT / "scripts/pack-sq2-dpu-display.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

def archive_entry(name, body, magic=b"070701", uid=123, gid=456, mode=0o100640):
    fields=(17,mode,uid,gid,1,1712345678,len(body),8,3,0,0,len(name.encode())+1,
            sum(body)&0xffffffff if magic==b"070702" else 0)
    return module.replacement((magic,fields,name.encode()+b"\0",body),body)

class ModuleMetadata(unittest.TestCase):
    def test_replacement_preserves_all_metadata_and_crc(self):
        name="lib/modules/test/kernel/drivers/gpu/drm/msm/msm.ko"
        for magic in (b"070701",b"070702"):
            original=module.records(archive_entry(name,b"old",magic))[name]
            replacement=module.records(module.replacement(original,b"new ELF payload"))[name]
            self.assertEqual(replacement[0],original[0])
            self.assertEqual(replacement[2],original[2])
            self.assertEqual(replacement[3],b"new ELF payload")
            for i in set(range(13))-{6,12}:
                self.assertEqual(replacement[1][i],original[1][i])
            self.assertEqual(module.verify_effective({name:original},{name:replacement},
                                                     name,b"new ELF payload"),[name])

    def test_credential_replay_ownership_change_rejected(self):
        name="lib/modules/test/kernel/drivers/gpu/drm/msm/msm.ko"
        credential=archive_entry("etc/shadow",b"synthetic",uid=10,gid=20)
        baseline=(archive_entry(name,b"old")+credential+
                  archive_entry("etc/shadow",b"synthetic",uid=30,gid=40))
        before=module.records(baseline)
        delta=module.replacement(before[name],b"new")
        changed=module.records(baseline+delta+credential)
        with self.assertRaises(ValueError):
            module.verify_effective(before,changed,name,b"new")
        # Replaying the final matching metadata is accepted, across gzip members too.
        final=archive_entry("etc/shadow",b"synthetic",uid=30,gid=40)
        output=gzip.compress(baseline)+gzip.compress(delta+final)
        self.assertEqual(module.verify_effective(before,module.records(gzip.decompress(output)),
                                                 name,b"new"),[name])

    def test_truncation_and_crc_corruption_rejected(self):
        data=archive_entry("module",b"payload",b"070702")
        with self.assertRaises(ValueError):
            module.records(data[:109])
        with self.assertRaises(ValueError):
            module.records(data[:-4])
        bad=bytearray(data)
        start=(110+len(b"module\0")+3)&~3
        bad[start]^=1
        with self.assertRaises(ValueError):
            module.records(bytes(bad))

if __name__=="__main__":
    unittest.main()
