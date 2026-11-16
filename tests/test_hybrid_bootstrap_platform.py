import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'scripts'))
import hybrid_bootstrap as b
import hybrid_bootstrap_platform as platform


def archive(path, files):
    with tarfile.open(path, 'w:gz') as tar:
        for name, data in files.items():
            info = tarfile.TarInfo(name); info.size = len(data)
            tar.addfile(info, io.BytesIO(data))


class SourceRecipeTest(unittest.TestCase):
    def test_public_kernel_recipe_and_corrupt_patch(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); (root/'configs').mkdir()
            imported = root/'imported'; (imported/'downloads').mkdir(parents=True)
            archive(imported/'downloads/linux.tar.gz', {'linux-6.18.60/Makefile': b'old\n'})
            archive(root/'external.tar.gz', {'external/example/source.c': b'/* source */\n'})
            (root/'kernel.patch').write_text('--- a/Makefile\n+++ b/Makefile\n@@ -1 +1 @@\n-old\n+new\n')
            pin = b.digest(imported/'downloads/linux.tar.gz')
            spec = {'downloads': [{'id':'linux-6.18.60','file':'linux.tar.gz','algorithm':'SHA256','hash':pin}]}
            (root/'configs/hybrid-dependencies.json').write_text(json.dumps(spec))
            recipe = {'upstream': {'sha256':pin}, 'patch':'kernel.patch','patch_sha256':b.digest(root/'kernel.patch'),
                      'external':'external.tar.gz','external_sha256':b.digest(root/'external.tar.gz')}
            (root/'configs/hybrid-kernel-source.json').write_text(json.dumps(recipe))
            with patch.object(b, 'ROOT', root):
                obj = b.Bootstrap(imported, root/'work'); platform.kernel(obj)
                result = root/'work/build-inputs/kernel-6.18/source'
                self.assertTrue(json.loads((result/'SOURCE.json').read_text())['current_release_patches'])
                with tarfile.open(result/'source.tar.gz') as tar:
                    self.assertEqual(tar.extractfile('kernel/Makefile').read(), b'new\n')
                    self.assertEqual(tar.extractfile('external/example/source.c').read(), b'/* source */\n')
                self.assertEqual(pin,b.digest(imported/'downloads/linux.tar.gz'))
                (root/'kernel.patch').write_text('corrupt')
                with self.assertRaisesRegex(ValueError,'source pin mismatch'):
                    platform.kernel(b.Bootstrap(imported, root/'bad'))
                self.assertFalse((root/'bad/kernel-source').exists())

    def test_hidl_tree_checked_before_unpack(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'configs').mkdir();(root/'imported/downloads').mkdir(parents=True)
            archive(root/'imported/downloads/headers.tar.gz',{'header.h':b'changed'})
            item={'id':'headers','file':'headers.tar.gz','algorithm':'SHA256',
                  'hash':b.digest(root/'imported/downloads/headers.tar.gz'),'git_tree':'0'*40,
                  'materialize':{'group':'hidl','destination':'include','archive':True}}
            (root/'configs/hybrid-dependencies.json').write_text(json.dumps({'downloads':[item]}))
            with patch.object(b,'ROOT',root):
                obj=b.Bootstrap(root/'imported',root/'work')
                with self.assertRaisesRegex(ValueError,'Git tree mismatch'):
                    platform.materialize_hidl(obj,root/'hidl')
            self.assertFalse((root/'hidl/include/header.h').exists())


if __name__ == '__main__': unittest.main()
